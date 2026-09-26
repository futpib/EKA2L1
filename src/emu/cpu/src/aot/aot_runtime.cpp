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
#include <cpu/aot/code_cache.h>
#include <common/performance.h>
#include <common/guest_profile.h>
#include <cpu/dyncom/armstate.h>
#include <common/log.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/12l1r/tlb.h>
#include <cpu/aot/arm_translator.h>
#include <algorithm>
#include <unordered_map>
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace eka2l1::arm::aot {
// Optional differential execution. Guest memory is changed only by compiled
// execution; the reference interpreter uses a private byte overlay.
static std::uint32_t hot_rom_base = 0, hot_rom_size = 0;
bool diagnostics_enabled = false;
bool validation_running = false;
static bool validating = false;
static ARMul_State *validation_guest = nullptr;
static core::thread_context validation_before;
static std::unordered_map<std::uint32_t, std::uint8_t> validation_memory;
static std::unordered_map<std::uint32_t, std::uint8_t> reference_writes;

static unsigned verification_stride() {
    static const unsigned stride = [] {
        const char *value = std::getenv("EKA2L1_AOT_VERIFY");
        return value ? std::max(1ul, std::strtoul(value, nullptr, 10)) : 0ul;
    }();
    return stride;
}

static inline void count_ram_dispatch(ARMul_State *cpu) {
    if (common::performance::counting() && ram_compilation_enabled
        && (cpu->Reg[15] < hot_rom_base || cpu->Reg[15] - hot_rom_base >= hot_rom_size))
        ++common::performance::ram_aot_dispatches;
}

void validation_begin(ARMul_State *cpu) {
    const auto stride = verification_stride();
    static std::uint64_t attempts = 0;
    validating = stride && (++attempts % stride == 0);
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



// Extend export-based compilation using deterministic dispatch samples. Only
// immutable ROM addresses are eligible; RAM code needs explicit invalidation.
bool hot_compilation_enabled = false;
bool ram_compilation_enabled = false;
bool chaining_enabled = false;
static bool region_enabled = false;
static validated_code_cache ram_cache;
static std::unordered_map<std::uint64_t, unsigned> ram_counts;
static const std::uint8_t *hot_rom = nullptr;
static std::uint64_t hot_dispatches = 0;
static std::uint32_t hot_compiled = 0;
static std::unordered_map<std::uint32_t, unsigned> hot_counts;
static std::vector<wasm_func_def> hot_pending;

static void flush_hot_blocks() {
    if (hot_pending.empty()) return;
    auto bytes = build_wasm_module(hot_pending, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    stage_aot_module(std::move(bytes), "hot-rom");
    instantiate_staged_modules();
    hot_pending.clear();
}

void configure_hot_rom(const std::uint8_t *host, std::uint32_t base, std::uint32_t size, bool enabled) {
    hot_rom = host; hot_rom_base = base; hot_rom_size = size;
    hot_compilation_enabled = enabled;
    hot_dispatches = 0; hot_compiled = 0; hot_counts.clear(); hot_pending.clear();
    ram_cache = {}; ram_counts.clear();
    const char *ram = std::getenv("EKA2L1_AOT_RAM");
    ram_compilation_enabled = enabled && ram && ram[0] == '1';
    const char *chain = std::getenv("EKA2L1_AOT_CHAIN");
    chaining_enabled = enabled && chain && chain[0] == '1';
    const char *region = std::getenv("EKA2L1_AOT_REGION");
    region_enabled = chaining_enabled && region && region[0] == '1';
}

void invalidate_ram_code(std::uint32_t address, std::size_t size) {
    ram_cache.invalidate(address, size);
}

aot_func lookup_compiled(ARMul_State *cpu) {
    if (validation_running) return nullptr;
    const auto pc = cpu->Reg[15], pc_mode = pc | cpu->TFlag;
    // Existing ROM functions use immutable bytes and need no mapping lookup.
    if (!ram_compilation_enabled || (pc >= hot_rom_base && pc - hot_rom_base < hot_rom_size)) {
        cpu->aot_code_begin = cpu->aot_code_end = 0; // ROM is immutable.
        auto function = global_registry().lookup(pc_mode);
        if ((common::guest_profile::enabled && common::performance::counting()) && !function) common::guest_profile::state.event("rom_missing",pc_mode);
        return function;
    }
    // The normal path avoids the resolver callback on a generation/space hit,
    // while comparing the exact compiled bytes on every entry.
    if (!(common::guest_profile::enabled && common::performance::counting())) {
        auto *entry = ram_cache.find(pc_mode, *cpu->parent());
        if (!entry) return nullptr;
        cpu->aot_code_begin = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(entry->backing));
        cpu->aot_code_end = cpu->aot_code_begin + static_cast<std::uint32_t>(entry->code.size());
        return entry->function;
    }
    core::code_mapping view;
    if (!cpu->parent()->resolve_code) return nullptr;
    const bool mapped = cpu->parent()->resolve_code(pc, view);
    auto *entry = ram_cache.find(pc_mode, view);
    if ((common::guest_profile::enabled && common::performance::counting()) && (!mapped || !entry || !entry->function)) {
        std::uint32_t opcode = 0;
        if (mapped && view.bytes && view.size >= (cpu->TFlag ? 2u : 4u)) std::memcpy(&opcode,view.bytes,cpu->TFlag ? 2 : 4);
        common::guest_profile::state.event(!mapped ? "ram_unmapped" : !entry ? "ram_missing" : entry->rejected ? "ram_rejected" : "ram_pending",pc_mode,view.address_space,opcode);
    }
    if (!mapped || !entry) return nullptr;
    cpu->aot_code_begin = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(view.bytes));
    cpu->aot_code_end = cpu->aot_code_begin + static_cast<std::uint32_t>(entry->code.size());
    return entry->function;
}

template<bool Verify>
static compiled_run execute_chain_impl(ARMul_State *cpu, aot_func function) {
    const auto budget = cpu->aot_budget;
    compiled_run result;
    while (function && result.instructions < budget && result.blocks < 64) {
        cpu->aot_budget = budget - result.instructions;
        count_ram_dispatch(cpu);
        if constexpr (Verify) validation_begin(cpu);
        auto *tlb = static_cast<dyncom_core *>(cpu->parent())->mem_cache();
        cpu->aot_tlb = !validating && tlb->page_bits == 12 ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(tlb->entries)) : 0;
        cpu->aot_exit = 0;
        const auto entry_pc = cpu->Reg[15] | cpu->TFlag;
        const auto count = function(cpu);
        if (common::guest_profile::enabled && common::performance::counting()) {
            auto &profile = common::guest_profile::state;
            ++profile.block_lengths[count];
            if (++profile.compiled_blocks % profile.stride == 0) {
                core::code_mapping view;
                if (cpu->parent()->resolve_code) cpu->parent()->resolve_code(entry_pc & ~1u,view);
                const auto width = (entry_pc & 1) ? 2u : 4u;
                std::uint32_t last = 0;
                if (!region_enabled && count && cpu->parent()->resolve_code && cpu->parent()->resolve_code((entry_pc & ~1u)+(count-1)*width,view)
                    && view.size >= width) std::memcpy(&last,view.bytes,width);
                profile.edge(entry_pc,cpu->Reg[15] | cpu->TFlag,view.address_space,count,last);
            }
        }
        if constexpr (Verify) validation_end(cpu, count);
        if (count > cpu->aot_budget) std::abort(); // generated-code contract
        if ((common::guest_profile::enabled && common::performance::counting()) && !count) common::guest_profile::state.event("compiled_zero",cpu->Reg[15] | cpu->TFlag);
        ++result.blocks;
        result.instructions += count;
        if (!count || !cpu->NumInstrsToExecute || result.instructions == budget || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) break;
        cpu->Reg[15] &= cpu->TFlag ? ~1u : ~3u;
        // This stays inside the compiled runner. Every RAM successor is validated;
        // no stale function pointer is linked across a mapping/code change.
        function = lookup_compiled(cpu);
    }
    return result;
}

compiled_run execute_chain(ARMul_State *cpu, aot_func function) {
    return verification_stride() ? execute_chain_impl<true>(cpu, function)
                                 : execute_chain_impl<false>(cpu, function);
}

std::uint32_t execute_single(ARMul_State *cpu, aot_func function) {
    count_ram_dispatch(cpu);
    if (!verification_stride()) return function(cpu);
    validation_begin(cpu);
    const auto count = function(cpu);
    validation_end(cpu, count);
    return count;
}

void observe_hot_pc(ARMul_State *cpu) {
#ifdef __EMSCRIPTEN__
    if (!hot_compilation_enabled || validation_running || (++hot_dispatches & 31)) return;
    if ((hot_dispatches & 8191) == 0) flush_hot_blocks();
    const auto pc = cpu->Reg[15], key = pc | cpu->TFlag;
    if (pc < hot_rom_base || pc - hot_rom_base >= hot_rom_size) {
        if (!ram_compilation_enabled || !cpu->parent()->resolve_code) return;
        if (ram_cache.versions() >= 16384) {
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("ram_capacity",key);
            return;
        }
        core::code_mapping view;
        if (!cpu->parent()->resolve_code(pc, view)) return;
        if (ram_cache.find(key, view)) return; // compiled or awaiting instantiation
        const auto identity = validated_code_cache::key(view.address_space, key);
        if (ram_counts.size() >= 131072 && !ram_counts.count(identity)) return;
        auto &count = ram_counts[identity];
        if (++count % 8) {
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("candidate_threshold",key,view.address_space);
            return;
        }
        const auto size = std::min(std::size_t(chaining_enabled ? 512 : 256), view.size);
        auto tr = cpu->TFlag ? translate_thumb_block(view.bytes, size, pc, nullptr, nullptr, true, true, chaining_enabled)
                            : translate_arm_block(view.bytes, size, pc, nullptr, nullptr, true, true, chaining_enabled, region_enabled);
        if (tr.func.body.empty() || !tr.entry_supported) {
            // Cache rejection against these exact bytes; retry only after mutation.
            ram_cache.insert(key, view, std::min(size, std::size_t(cpu->TFlag ? 2 : 4))).rejected = true;
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("compile_rejected",key,view.address_space);
            return;
        }
        // Only emitted instructions depend on these bytes. The rest of the
        // translation window is unused after a store/terminator; validating it
        // on millions of short-block entries needlessly scans unrelated code.
        const auto consumed = std::clamp(std::size_t(tr.end_address - pc),
            std::size_t(cpu->TFlag ? 2 : 4), size);
        auto &entry = ram_cache.insert(key, view, consumed);
        tr.func.export_name = "r_" + std::to_string(entry.version);
        hot_pending.push_back(std::move(tr.func));
        if (common::performance::counting()) ++common::performance::ram_blocks_compiled;
        if (hot_pending.size() >= 32) flush_hot_blocks();
        return;
    }
    if (hot_compiled >= 4096) return;
    if (hot_counts.size() >= 65536 && !hot_counts.count(key)) return;
    auto &count = hot_counts[key];
    if (count >= 8 || ++count != 8) return; // one attempt per immutable entry
    const auto offset = pc - hot_rom_base;
    const auto size = std::min(chaining_enabled ? 512u : 128u, hot_rom_size - offset);
    auto tr = cpu->TFlag ? translate_thumb_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, chaining_enabled)
                        : translate_arm_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, chaining_enabled, region_enabled);
    if (tr.func.body.empty() || !tr.entry_supported) return;
    tr.func.export_name = "f_" + std::to_string(key);
    hot_pending.push_back(std::move(tr.func));
    if (hot_pending.size() >= 32) flush_hot_blocks();
    ++hot_compiled;
#endif
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

// Uninstrumented internal imports. The checked variants above are selected only
// for verifier runs; ordinary memory accesses contain no validation hook.
static std::uint32_t raw_read32(ARMul_State *s, std::uint32_t a) { return s->ReadMemory32(a); }
static std::uint32_t raw_read16(ARMul_State *s, std::uint32_t a) { return s->ReadMemory16(a); }
static std::uint32_t raw_read8(ARMul_State *s, std::uint32_t a) { return s->ReadMemory8(a); }
static void raw_write32(ARMul_State *s, std::uint32_t a, std::uint32_t v) { s->WriteMemory32(a, v); }
static void raw_write16(ARMul_State *s, std::uint32_t a, std::uint16_t v) { s->WriteMemory16(a, v); }
static void raw_write8(ARMul_State *s, std::uint32_t a, std::uint8_t v) { s->WriteMemory8(a, v); }

template<unsigned Index> static void count_memory() {
    if (common::performance::counting()) ++common::guest_profile::state.memory_calls[Index];
}
static std::uint32_t prof_read32(ARMul_State *s, std::uint32_t a) { count_memory<0>(); return raw_read32(s,a); }
static void prof_write32(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<1>(); raw_write32(s,a,v); }
static std::uint32_t prof_read8(ARMul_State *s, std::uint32_t a) { count_memory<2>(); return raw_read8(s,a); }
static void prof_write8(ARMul_State *s, std::uint32_t a, std::uint8_t v) { count_memory<3>(); raw_write8(s,a,v); }
static std::uint32_t prof_read16(ARMul_State *s, std::uint32_t a) { count_memory<4>(); return raw_read16(s,a); }
static void prof_write16(ARMul_State *s, std::uint32_t a, std::uint16_t v) { count_memory<5>(); raw_write16(s,a,v); }

// JS function that instantiates a WASM module and returns exported function
// addresses as a comma-separated string of "name:table_idx" pairs.
// Returns empty string on failure.
EM_JS(char*, js_instantiate_aot_module, (const uint8_t* bytes, int len, const std::uintptr_t *helpers), {
    try {
        var wasmBytes = new Uint8Array(wasmMemory.buffer, bytes, len);
        // Copy the bytes — the buffer may be detached during instantiation
        wasmBytes = new Uint8Array(wasmBytes);
        var wasmModule = new WebAssembly.Module(wasmBytes);

        // wasmExports AND wasmTable.get can be JS abort wrappers. Native table
        // access obtains the C++ function pointers as genuine WASM functions.
        // Outer export/thread-entry abort handling remains enabled unchanged.
        var raw = index => WebAssembly.Table.prototype.get.call(wasmTable, HEAPU32[(helpers >>> 2) + index]);
        var importObj = {env: {
            memory: wasmMemory,
            tlb_read32: raw(0), tlb_write32: raw(1),
            tlb_read8: raw(2), tlb_write8: raw(3),
            tlb_read16: raw(4), tlb_write16: raw(5)
        }};

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
    if (dll_name != "hot-rom") fprintf(stderr, "AOT: staging %zu-byte WASM module for %s\n",
        wasm_bytes.size(), dll_name.c_str());
    g_staged_modules.push_back({std::move(wasm_bytes), dll_name});
}

static int do_instantiate(const std::vector<std::uint8_t> &wasm_bytes,
    const std::string &dll_name)
{
    if (wasm_bytes.empty()) return 0;

    const bool verify = verification_stride() != 0;
    const std::uintptr_t helpers[] = {
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_read32 : common::guest_profile::enabled ? prof_read32 : raw_read32),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_write32 : common::guest_profile::enabled ? prof_write32 : raw_write32),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_read8 : common::guest_profile::enabled ? prof_read8 : raw_read8),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_write8 : common::guest_profile::enabled ? prof_write8 : raw_write8),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_read16 : common::guest_profile::enabled ? prof_read16 : raw_read16),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_write16 : common::guest_profile::enabled ? prof_write16 : raw_write16)
    };
    char *result_str = js_instantiate_aot_module(wasm_bytes.data(),
        static_cast<int>(wasm_bytes.size()), helpers);

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
        if (name.substr(0, 2) == "r_") {
            ram_cache.attach(static_cast<std::uint32_t>(std::stoul(name.substr(2))),
                reinterpret_cast<aot_func>(table_idx));
            ++count;
            continue;
        }
        if (name.substr(0, 2) != "f_") continue;
        std::uint32_t addr = static_cast<std::uint32_t>(std::stoul(name.substr(2)));

        // Convert table index to function pointer
        auto func = reinterpret_cast<aot_func>(table_idx);
        reg.register_function(addr, func);
        count++;


    }

    if (dll_name != "hot-rom") LOG_INFO(CPU_DYNCOM, "AOT: instantiated {} functions for {}", count, dll_name);
    return count;
}

bool instantiate_staged_modules() {
    if (g_staged_modules.empty()) return false;



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
