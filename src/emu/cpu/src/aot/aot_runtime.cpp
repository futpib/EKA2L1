/*
 * Copyright (c) 2024 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/aot_registry.h>
#include <cpu/dyncom/armstate.h>
#include <common/log.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <unordered_map>
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace eka2l1::arm::aot {
// Optional differential execution. Guest memory is changed only by compiled
// execution; the reference interpreter uses a private byte overlay.
bool validation_running = false;
static bool validating = false;
static ARMul_State *validation_guest = nullptr;
static core::thread_context validation_before;
static std::unordered_map<std::uint32_t, std::uint8_t> validation_memory;
static std::unordered_map<std::uint32_t, std::uint8_t> reference_writes;

void validation_begin(ARMul_State *cpu) {
    static const bool enabled = std::getenv("EKA2L1_AOT_VERIFY") != nullptr;
    validating = enabled;
    if (!validating) return;
    validation_guest = cpu;
    cpu->parent()->save_context(validation_before);
    validation_before.cpsr = (cpu->Cpsr & 0x0fffffdf) | (cpu->NFlag << 31)
        | (cpu->ZFlag << 30) | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
    validation_memory.clear(); reference_writes.clear();
}

static void validation_access(ARMul_State *cpu, std::uint32_t addr, unsigned size) {
    if (!validating) return;
    for (unsigned i = 0; i < size; ++i)
        if (!validation_memory.count(addr + i)) validation_memory[addr+i] = cpu->ReadMemory8(addr+i);
}

template<typename T> static bool reference_read(std::uint32_t addr, T *value) {
    auto *bytes = reinterpret_cast<std::uint8_t *>(value);
    for (unsigned i = 0; i < sizeof(T); ++i) {
        const auto a = addr + i;
        auto w = reference_writes.find(a);
        auto v = validation_memory.find(a);
        bytes[i] = w != reference_writes.end() ? w->second
            : v != validation_memory.end() ? v->second : validation_guest->ReadMemory8(a);
    }
    return true;
}
template<typename T> static bool reference_write(std::uint32_t addr, T *value) {
    const auto *bytes = reinterpret_cast<std::uint8_t *>(value);
    for (unsigned i = 0; i < sizeof(T); ++i) reference_writes[addr+i] = bytes[i];
    return true;
}

void validation_end(ARMul_State *cpu, std::uint32_t count) {
    if (!validating || !count) { validating = false; return; }
    static r12l1::exclusive_monitor monitor(1);
    static dyncom_core reference(&monitor, 12);
    reference.read_code = [cpu](address addr, std::uint32_t *v) { return cpu->parent()->read_code(addr, v); };
    reference.read_8bit = reference_read<std::uint8_t>; reference.write_8bit = reference_write<std::uint8_t>;
    reference.read_16bit = reference_read<std::uint16_t>; reference.write_16bit = reference_write<std::uint16_t>;
    reference.read_32bit = reference_read<std::uint32_t>; reference.write_32bit = reference_write<std::uint32_t>;
    reference.read_64bit = reference_read<std::uint64_t>; reference.write_64bit = reference_write<std::uint64_t>;
    reference.load_context(validation_before);
    validation_running = true;
    reference.run(count);
    validation_running = false;
    bool same = true;
    for (unsigned r = 0; r < 16; ++r) {
        auto actual = cpu->Reg[r];
        if (r == 15) actual &= cpu->TFlag ? ~1u : ~3u;
        if (actual != reference.get_reg(r)) {
            fprintf(stderr, "AOT VERIFY pc=%08X count=%u R%u compiled=%08X reference=%08X\n",
                validation_before.cpu_registers[15],count,r,actual,reference.get_reg(r));
            same = false;
        }
    }
    const auto flags = (cpu->NFlag<<31)|(cpu->ZFlag<<30)|(cpu->CFlag<<29)|(cpu->VFlag<<28)|(cpu->TFlag<<5);
    if ((reference.get_cpsr() & 0xF0000020) != flags) {
        fprintf(stderr,"AOT VERIFY flags pc=%08X count=%u compiled=%08X reference=%08X\n",
            validation_before.cpu_registers[15],count,flags,reference.get_cpsr()); same=false;
    }
    for (const auto &[addr, value] : validation_memory) {
        auto it = reference_writes.find(addr);
        const auto expected = it == reference_writes.end() ? value : it->second;
        if (cpu->ReadMemory8(addr) != expected) { fprintf(stderr,"AOT VERIFY memory %08X\n",addr);same=false;break; }
    }
    for (const auto &[addr, value] : reference_writes)
        if (cpu->ReadMemory8(addr) != value) { fprintf(stderr,"AOT VERIFY reference write %08X\n",addr);same=false;break; }
    if (!same) {
        fprintf(stderr,"AOT VERIFY entry mode=%u code:", (validation_before.cpsr>>5)&1);
        for (unsigned i=0;i<32;i+=4) fprintf(stderr," %08X",cpu->ReadCode(validation_before.cpu_registers[15]+i));
        fprintf(stderr,"\n");
        for(unsigned i=0;i<16;++i) fprintf(stderr," r%u=%08X",i,validation_before.cpu_registers[i]);
        fprintf(stderr,"\n");
        std::abort();
    }
    validating = false;
}



#ifdef __EMSCRIPTEN__

// C functions exported for the AOT WASM module to call back into.
// These are the memory access trampolines.
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read32(ARMul_State *state, std::uint32_t arm_addr) {
        validation_access(state, arm_addr, 4);
        return state->ReadMemory32(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write32(ARMul_State *state, std::uint32_t arm_addr, std::uint32_t value) {
        validation_access(state, arm_addr, 4);
        state->WriteMemory32(arm_addr, value);
    }

    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read8(ARMul_State *state, std::uint32_t arm_addr) {
        validation_access(state, arm_addr, 1);
        return state->ReadMemory8(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read16(ARMul_State *state, std::uint32_t arm_addr) {
        validation_access(state, arm_addr, 2);
        return state->ReadMemory16(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write16(ARMul_State *state, std::uint32_t arm_addr, std::uint16_t value) {
        validation_access(state, arm_addr, 2);
        state->WriteMemory16(arm_addr, value);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write8(ARMul_State *state, std::uint32_t arm_addr, std::uint8_t value) {
        validation_access(state, arm_addr, 1);
        state->WriteMemory8(arm_addr, value);
    }
}

// JS function that instantiates a WASM module and returns exported function
// addresses as a comma-separated string of "name:table_idx" pairs.
// Returns empty string on failure.
EM_JS(char*, js_instantiate_aot_module, (const uint8_t* bytes, int len), {
    try {
        var wasmBytes = new Uint8Array(wasmMemory.buffer, bytes, len);
        // Copy the bytes — the buffer may be detached during instantiation
        wasmBytes = new Uint8Array(wasmBytes);
        var wasmModule = new WebAssembly.Module(wasmBytes);

        // Import tlb functions DIRECTLY from wasmExports (raw WASM functions,
        // bypassing Module._* which is wrapped by createExportWrapper).
        // This allows the WASM optimizer to turn these into inline calls.
        var importObj = {
            env: {
                memory: wasmMemory, // Emscripten's shared memory
                tlb_read32: wasmExports.aot_tlb_read32,
                tlb_write32: wasmExports.aot_tlb_write32,
                tlb_read8: wasmExports.aot_tlb_read8,
                tlb_write8: wasmExports.aot_tlb_write8,
            }
        };

        var instance = new WebAssembly.Instance(wasmModule, importObj);

        // Collect exports and add them to Emscripten's function table
        var results = [];
        for (var name in instance.exports) {
            if (name === 'memory') continue;
            var func = instance.exports[name];
            if (typeof func !== 'function') continue;

            // Add to Emscripten's indirect function table
            var tableIdx = addFunction(func, 'ii'); // (i32) -> i32
            results.push(name + ':' + tableIdx);
        }

        var resultStr = results.join(',');
        var len = lengthBytesUTF8(resultStr) + 1;
        var ptr = _malloc(len);
        stringToUTF8(resultStr, ptr, len);
        return ptr;
    } catch(e) {
        err('AOT instantiation failed: ' + e.message);
        var ptr = _malloc(1);
        new Uint8Array(wasmMemory.buffer)[ptr] = 0;
        return ptr;
    }
});

// Staged modules waiting for worker-thread instantiation
struct staged_module {
    std::vector<std::uint8_t> wasm_bytes;
    std::string dll_name;
};
static std::vector<staged_module> g_staged_modules;

void stage_aot_module(
    std::vector<std::uint8_t> wasm_bytes,
    const std::string &dll_name)
{
    fprintf(stderr, "AOT: staging %zu-byte WASM module for %s\n",
        wasm_bytes.size(), dll_name.c_str());
    g_staged_modules.push_back({std::move(wasm_bytes), dll_name});
}

static int do_instantiate(const std::vector<std::uint8_t> &wasm_bytes,
    const std::string &dll_name)
{
    if (wasm_bytes.empty()) return 0;

    char *result_str = js_instantiate_aot_module(wasm_bytes.data(),
        static_cast<int>(wasm_bytes.size()));

    if (!result_str || result_str[0] == '\0') {
        if (result_str) free(result_str);
        LOG_ERROR(CPU_DYNCOM, "AOT: failed to instantiate WASM module for {}", dll_name);
        return 0;
    }

    // Parse "f_12345:42,f_67890:43,..."
    std::string results(result_str);
    free(result_str);

    int count = 0;
    registry &reg = global_registry();

    std::size_t pos = 0;
    while (pos < results.size()) {
        std::size_t comma = results.find(',', pos);
        if (comma == std::string::npos) comma = results.size();

        std::string entry = results.substr(pos, comma - pos);
        pos = comma + 1;

        std::size_t colon = entry.find(':');
        if (colon == std::string::npos) continue;

        std::string name = entry.substr(0, colon);
        int table_idx = std::atoi(entry.substr(colon + 1).c_str());

        // Parse address from "f_<decimal_addr>"
        if (name.substr(0, 2) != "f_") continue;
        std::uint32_t addr = static_cast<std::uint32_t>(std::stoul(name.substr(2)));

        // Convert table index to function pointer
        auto func = reinterpret_cast<aot_func>(table_idx);
        reg.register_function(addr, func);
        count++;

        LOG_INFO(CPU_DYNCOM, "AOT: registered {} at 0x{:08X} (table idx {})", name, addr, table_idx);
    }

    LOG_INFO(CPU_DYNCOM, "AOT: instantiated {} functions for {}", count, dll_name);
    return count;
}

bool instantiate_staged_modules() {
    if (g_staged_modules.empty()) return false;

    fprintf(stderr, "AOT: instantiating %zu staged module(s) on worker thread\n",
        g_staged_modules.size());

    for (auto &mod : g_staged_modules) {
        do_instantiate(mod.wasm_bytes, mod.dll_name);
    }
    g_staged_modules.clear();
    return true;
}

#else // !__EMSCRIPTEN__

void stage_aot_module(
    std::vector<std::uint8_t>,
    const std::string &)
{
}

bool instantiate_staged_modules() {
    return false;
}

#endif // __EMSCRIPTEN__

}
