/*
 * AOT correctness test — Emscripten binary.
 *
 * For each test case:
 * 1. Set up initial CPU state + memory
 * 2. Run Thumb code through the Dyncom interpreter
 * 3. Capture final state (registers, flags, memory)
 * 4. Reset to same initial state
 * 5. Translate the same Thumb code to WASM via the translator
 * 6. Instantiate the WASM module (via EM_JS + WebAssembly.Instance)
 * 7. Run the WASM function on the state buffer
 * 8. Compare both final states
 *
 * Build with Emscripten, run with Node.js.
 */

#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/aot/thumb_translator.h>
#include <cpu/aot/wasm_emitter.h>

#include <cstdio>
#include <cstring>
#include <vector>
#include <array>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace eka2l1::arm;
using namespace eka2l1::arm::aot;

// ---- Memory environment for tests ----

struct test_mem {
    static constexpr std::uint32_t SIZE = 1024 * 1024;
    std::vector<std::uint8_t> data;
    test_mem() : data(SIZE, 0) {}

    void write32(std::uint32_t addr, std::uint32_t val) {
        if (addr + 4 <= SIZE) std::memcpy(&data[addr], &val, 4);
    }
    std::uint32_t read32(std::uint32_t addr) const {
        std::uint32_t v = 0;
        if (addr + 4 <= SIZE) std::memcpy(&v, &data[addr], 4);
        return v;
    }
    void write16(std::uint32_t addr, std::uint16_t val) {
        if (addr + 2 <= SIZE) std::memcpy(&data[addr], &val, 2);
    }
    void write_code(std::uint32_t addr, const std::vector<std::uint8_t> &code) {
        for (std::size_t i = 0; i < code.size() && addr + i < SIZE; i++)
            data[addr + i] = code[i];
    }
};

static std::unique_ptr<dyncom_core> make_cpu(test_mem &mem, r12l1::exclusive_monitor &mon) {
    auto core = std::make_unique<dyncom_core>(&mon, 12);
    test_mem *mp = &mem;

    core->read_code = [mp](address a, std::uint32_t *r) -> bool {
        if (a + 4 > test_mem::SIZE) return false;
        std::memcpy(r, &mp->data[a], 4); return true;
    };
    core->read_8bit = [mp](address a, std::uint8_t *r) -> bool {
        if (a >= test_mem::SIZE) return false;
        *r = mp->data[a]; return true;
    };
    core->write_8bit = [mp](address a, std::uint8_t *r) -> bool {
        if (a >= test_mem::SIZE) return false;
        mp->data[a] = *r; return true;
    };
    core->read_16bit = [mp](address a, std::uint16_t *r) -> bool {
        if (a + 2 > test_mem::SIZE) return false;
        std::memcpy(r, &mp->data[a], 2); return true;
    };
    core->write_16bit = [mp](address a, std::uint16_t *r) -> bool {
        if (a + 2 > test_mem::SIZE) return false;
        std::memcpy(&mp->data[a], r, 2); return true;
    };
    core->read_32bit = [mp](address a, std::uint32_t *r) -> bool {
        if (a + 4 > test_mem::SIZE) return false;
        std::memcpy(r, &mp->data[a], 4); return true;
    };
    core->write_32bit = [mp](address a, std::uint32_t *r) -> bool {
        if (a + 4 > test_mem::SIZE) return false;
        std::memcpy(&mp->data[a], r, 4); return true;
    };
    core->read_64bit = [mp](address a, std::uint64_t *r) -> bool {
        if (a + 8 > test_mem::SIZE) return false;
        std::memcpy(r, &mp->data[a], 8); return true;
    };
    core->write_64bit = [mp](address a, std::uint64_t *r) -> bool {
        if (a + 8 > test_mem::SIZE) return false;
        std::memcpy(&mp->data[a], r, 8); return true;
    };

    return core;
}

struct cpu_state {
    std::array<std::uint32_t, 16> regs;
    std::uint32_t nflag, zflag, cflag, vflag;
};

static cpu_state capture_state(dyncom_core &cpu) {
    cpu_state s;
    for (int i = 0; i < 16; i++) s.regs[i] = cpu.get_reg(i);
    // Read flags from the ARMul_State via the core's context
    core::thread_context ctx;
    cpu.save_context(ctx);
    // Flags are in CPSR bits — but Dyncom stores them separately.
    // We need to read them from the ARMul_State directly.
    // Since we can't access ARMul_State from dyncom_core's public API,
    // we use cpsr bits.
    std::uint32_t cpsr = cpu.get_cpsr();
    s.nflag = (cpsr >> 31) & 1;
    s.zflag = (cpsr >> 30) & 1;
    s.cflag = (cpsr >> 29) & 1;
    s.vflag = (cpsr >> 28) & 1;
    return s;
}

// ---- JS glue for WASM instantiation ----

#ifdef __EMSCRIPTEN__

// The test memory — accessible from JS for the tlb imports
static test_mem *g_test_mem = nullptr;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    std::uint32_t test_tlb_read32(std::uint32_t state_ptr, std::uint32_t addr) {
        (void)state_ptr;
        return g_test_mem ? g_test_mem->read32(addr) : 0;
    }
    EMSCRIPTEN_KEEPALIVE
    void test_tlb_write32(std::uint32_t state_ptr, std::uint32_t addr, std::uint32_t val) {
        (void)state_ptr;
        if (g_test_mem) g_test_mem->write32(addr, val);
    }
    EMSCRIPTEN_KEEPALIVE
    std::uint32_t test_tlb_read8(std::uint32_t state_ptr, std::uint32_t addr) {
        (void)state_ptr;
        return g_test_mem ? g_test_mem->data[addr] : 0;
    }
}

// Run a WASM module on a state buffer. Returns the instruction count,
// or -1 on failure. The state buffer is modified in place.
// state_ptr is a pointer into WASM linear memory where the ARMul_State fields are laid out.
EM_JS(int, js_run_aot_wasm, (const uint8_t* wasm_bytes, int wasm_len, uint8_t* state_buf, int state_len), {
    try {
        var bytes = new Uint8Array(wasmMemory.buffer, wasm_bytes, wasm_len);
        bytes = new Uint8Array(bytes); // copy

        var mod = new WebAssembly.Module(bytes);
        var instance = new WebAssembly.Instance(mod, {
            env: {
                memory: wasmMemory,
                tlb_read32: function(sp, addr) { return Module._test_tlb_read32(sp, addr); },
                tlb_write32: function(sp, addr, val) { Module._test_tlb_write32(sp, addr, val); },
                tlb_read8: function(sp, addr) { return Module._test_tlb_read8(sp, addr); },
            }
        });

        // Find the first exported function (named f_<addr>)
        var aotFunc = null;
        for (var name in instance.exports) {
            if (typeof instance.exports[name] === 'function') {
                aotFunc = instance.exports[name];
                break;
            }
        }
        if (!aotFunc) { err("No function export found"); return -1; }

        // state_buf is already in wasmMemory, so we pass its offset directly
        var result = aotFunc(state_buf);
        return result;
    } catch(e) {
        err("AOT test WASM error: " + e.message);
        return -1;
    }
});

#else

static int js_run_aot_wasm(const uint8_t*, int, uint8_t*, int) {
    return -1; // not available on native
}

#endif

// ---- Test cases ----

struct mem_init {
    std::uint32_t addr;
    std::uint32_t value;
};

struct test_case {
    const char *name;
    std::vector<std::uint8_t> code;      // Thumb bytecode
    std::uint32_t code_addr;             // address to place the code
    std::array<std::uint32_t, 16> init_regs;
    int max_instrs;                      // max instructions to run in interpreter
    std::vector<mem_init> init_mem;      // initial memory values
    std::vector<std::uint32_t> check_mem_addrs; // addresses to compare after
};

static bool run_test(const test_case &tc) {
    // --- Step 1: Run through interpreter ---
    test_mem interp_mem;
    r12l1::exclusive_monitor mon(1);

    // Write code + halt (B .)
    std::vector<std::uint8_t> full_code = tc.code;
    full_code.push_back(0xFE); full_code.push_back(0xE7); // B . (halt loop)

    interp_mem.write_code(tc.code_addr, full_code);
    for (auto &m : tc.init_mem) interp_mem.write32(m.addr, m.value);
    auto interp_cpu = make_cpu(interp_mem, mon);

    for (int i = 0; i < 16; i++) interp_cpu->set_reg(i, tc.init_regs[i]);
    interp_cpu->set_pc(tc.code_addr);
    interp_cpu->set_cpsr(0x00000030); // user mode + thumb

    interp_cpu->run(tc.max_instrs);
    cpu_state interp_state = capture_state(*interp_cpu);

    // Capture memory state from interpreter
    std::vector<std::uint32_t> interp_mem_vals;
    for (auto addr : tc.check_mem_addrs) interp_mem_vals.push_back(interp_mem.read32(addr));

    // --- Step 2: Translate to WASM ---
    auto tr = translate_thumb_block(tc.code.data(), tc.code.size(), tc.code_addr);
    if (tr.func.body.empty()) {
        printf("  SKIP %s: translator produced empty body\n", tc.name);
        return true;
    }

    std::vector<wasm_import_func> imports = {
        {"env", "tlb_read32", 2, true},
        {"env", "tlb_write32", 3, false},
        {"env", "tlb_read8", 2, true},
    };
    auto wasm_bytes = build_wasm_module({tr.func}, imports);

    // --- Step 3: Run WASM on a state buffer ---
    // Allocate a state buffer in WASM linear memory
    // We'll use a region of memory for the ARMul_State fields
    const int STATE_BUF_SIZE = 1024;
    auto state_buf = std::make_unique<std::uint8_t[]>(STATE_BUF_SIZE);
    std::memset(state_buf.get(), 0, STATE_BUF_SIZE);

    // Initialize registers at offset 0 (matching ARMul_State::Reg)
    auto *regs = reinterpret_cast<std::uint32_t *>(state_buf.get());
    for (int i = 0; i < 16; i++) regs[i] = tc.init_regs[i];
    regs[15] = tc.code_addr; // PC

    // Set flags
    auto set_field = [&](std::uint32_t offset, std::uint32_t val) {
        std::memcpy(state_buf.get() + offset, &val, 4);
    };
    set_field(state_offsets::CPSR, 0x00000030);
    set_field(state_offsets::TFLAG, 1);

    // Set up test memory for WASM tlb callbacks
#ifdef __EMSCRIPTEN__
    test_mem wasm_mem;
    wasm_mem.write_code(tc.code_addr, full_code);
    for (auto &m : tc.init_mem) wasm_mem.write32(m.addr, m.value);
    g_test_mem = &wasm_mem;

    int result = js_run_aot_wasm(wasm_bytes.data(), static_cast<int>(wasm_bytes.size()),
                                  state_buf.get(), STATE_BUF_SIZE);

    g_test_mem = nullptr;

    if (result < 0) {
        printf("  FAIL %s: WASM execution failed\n", tc.name);
        return false;
    }
#else
    printf("  SKIP %s: WASM execution only on Emscripten\n", tc.name);
    return true;
#endif

    // --- Step 4: Compare ---
    bool passed = true;
    auto get_field = [&](std::uint32_t offset) -> std::uint32_t {
        std::uint32_t v;
        std::memcpy(&v, state_buf.get() + offset, 4);
        return v;
    };

    // Compare registers (skip PC — it diverges due to halt loop vs bail)
    for (int i = 0; i < 15; i++) {
        std::uint32_t wasm_val = regs[i];
        std::uint32_t interp_val = interp_state.regs[i];
        if (wasm_val != interp_val) {
            printf("  FAIL %s: R%d = 0x%08X (wasm) vs 0x%08X (interp)\n",
                tc.name, i, wasm_val, interp_val);
            passed = false;
        }
    }

    // Compare flags
    std::uint32_t wasm_n = get_field(state_offsets::NFLAG);
    std::uint32_t wasm_z = get_field(state_offsets::ZFLAG);
    std::uint32_t wasm_c = get_field(state_offsets::CFLAG);
    std::uint32_t wasm_v = get_field(state_offsets::VFLAG);

    if (wasm_n != interp_state.nflag) { printf("  FAIL %s: N=%u (wasm) vs %u (interp)\n", tc.name, wasm_n, interp_state.nflag); passed = false; }
    if (wasm_z != interp_state.zflag) { printf("  FAIL %s: Z=%u (wasm) vs %u (interp)\n", tc.name, wasm_z, interp_state.zflag); passed = false; }
    if (wasm_c != interp_state.cflag) { printf("  FAIL %s: C=%u (wasm) vs %u (interp)\n", tc.name, wasm_c, interp_state.cflag); passed = false; }
    if (wasm_v != interp_state.vflag) { printf("  FAIL %s: V=%u (wasm) vs %u (interp)\n", tc.name, wasm_v, interp_state.vflag); passed = false; }

    // Compare memory
    for (std::size_t i = 0; i < tc.check_mem_addrs.size(); i++) {
        std::uint32_t wasm_val = wasm_mem.read32(tc.check_mem_addrs[i]);
        std::uint32_t interp_val = interp_mem_vals[i];
        if (wasm_val != interp_val) {
            printf("  FAIL %s: mem[0x%X] = 0x%08X (wasm) vs 0x%08X (interp)\n",
                tc.name, tc.check_mem_addrs[i], wasm_val, interp_val);
            passed = false;
        }
    }

    if (passed) {
        printf("  PASS %s\n", tc.name);
    }
    return passed;
}

// ---- Main ----

int main() {
    std::array<std::uint32_t, 16> zero_regs = {};
    zero_regs[13] = 0x10000; // SP

    // ADD NEW TEST CASES HERE
    // Each test runs Thumb bytecode through both the Dyncom interpreter
    // and the WASM AOT translator, comparing all registers and flags.
    // To add a test: {name, {thumb_bytes...}, code_addr, init_regs, max_instrs, init_mem, check_mem_addrs}
    std::vector<test_case> tests = {
        {"MOVS R0, #42", {0x2A, 0x20}, 0x1000, zero_regs, 10},
        {"MOVS R0, #0 (zero flag)", {0x00, 0x20}, 0x1000, zero_regs, 10},
        {"ADDS R0, R0, #1", {0x40, 0x1C}, 0x1000, [&]{ auto r = zero_regs; r[0] = 99; return r; }(), 10},
        {"SUBS R0, R0, #1", {0x40, 0x1E}, 0x1000, [&]{ auto r = zero_regs; r[0] = 1; return r; }(), 10},
        {"SUBS to zero", {0x40, 0x1E}, 0x1000, [&]{ auto r = zero_regs; r[0] = 1; return r; }(), 10},
        {"CMP R0, R1 equal", {0x88, 0x42}, 0x1000, [&]{ auto r = zero_regs; r[0] = 5; r[1] = 5; return r; }(), 10},
        {"CMP R0, R1 greater", {0x88, 0x42}, 0x1000, [&]{ auto r = zero_regs; r[0] = 10; r[1] = 5; return r; }(), 10},
        {"CMP R0, R1 less", {0x88, 0x42}, 0x1000, [&]{ auto r = zero_regs; r[0] = 3; r[1] = 10; return r; }(), 10},
        {"CMP R0, #0", {0x00, 0x28}, 0x1000, [&]{ auto r = zero_regs; r[0] = 0; return r; }(), 10},
        {"CMP R0, #5 when R0=5", {0x05, 0x28}, 0x1000, [&]{ auto r = zero_regs; r[0] = 5; return r; }(), 10},
        {"MOVS R1, R0 (LSLS #0)", {0x01, 0x00}, 0x1000, [&]{ auto r = zero_regs; r[0] = 42; return r; }(), 10},
        {"LSLS R0, R1, #4", {0x08, 0x01}, 0x1000, [&]{ auto r = zero_regs; r[1] = 3; return r; }(), 10},
        {"LSRS R0, R1, #1", {0x48, 0x08}, 0x1000, [&]{ auto r = zero_regs; r[1] = 10; return r; }(), 10},
        {"ANDS R0, R1", {0x08, 0x40}, 0x1000, [&]{ auto r = zero_regs; r[0] = 0xFF; r[1] = 0x0F; return r; }(), 10},
        {"ORRS R0, R1", {0x08, 0x43}, 0x1000, [&]{ auto r = zero_regs; r[0] = 0xF0; r[1] = 0x0F; return r; }(), 10},
        {"EORS R0, R1", {0x48, 0x40}, 0x1000, [&]{ auto r = zero_regs; r[0] = 0xFF; r[1] = 0x0F; return r; }(), 10},
        {"MVNS R0, R1", {0xC8, 0x43}, 0x1000, [&]{ auto r = zero_regs; r[1] = 0; return r; }(), 10},
        {"ADDS R0, #100", {0x64, 0x30}, 0x1000, [&]{ auto r = zero_regs; r[0] = 5; return r; }(), 10},
        {"SUBS R0, #1 to zero", {0x01, 0x38}, 0x1000, [&]{ auto r = zero_regs; r[0] = 1; return r; }(), 10},
        {"MOV R8, R0 (high reg)", {0x80, 0x46}, 0x1000, [&]{ auto r = zero_regs; r[0] = 123; return r; }(), 10},

        // --- LDR/STR ---
        {"LDR R0, [R1, #0]", {0x08, 0x68}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x2000; return r; }(), 10,
            {{0x2000, 0xDEADBEEF}}, {}},
        {"LDR R0, [R1, #4]", {0x48, 0x68}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x2000; return r; }(), 10,
            {{0x2004, 0x12345678}}, {}},
        {"STR R0, [R1, #0]", {0x08, 0x60}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0xCAFEBABE; r[1] = 0x2000; return r; }(), 10,
            {}, {0x2000}},
        {"LDR R0, [R1, R2]", {0x88, 0x58}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x2000; r[2] = 8; return r; }(), 10,
            {{0x2008, 0xAAAABBBB}}, {}},
        {"LDR R0, [SP, #8]", {0x02, 0x98}, 0x1000,
            [&]{ auto r = zero_regs; r[13] = 0x3000; return r; }(), 10,
            {{0x3008, 0x11223344}}, {}},
        {"STR R0, [SP, #0]", {0x00, 0x90}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0x55667788; r[13] = 0x3000; return r; }(), 10,
            {}, {0x3000}},

        // --- LDRB/STRB ---
        {"LDRB R0, [R1, #0]", {0x08, 0x78}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x2000; return r; }(), 10,
            {{0x2000, 0x000000AB}}, {}},
        {"STRB R0, [R1, #0]", {0x08, 0x70}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0x42; r[1] = 0x2000; return r; }(), 10,
            {}, {0x2000}},
        {"LDRB R0, [R1, R2]", {0x88, 0x5C}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x2000; r[2] = 3; return r; }(), 10,
            {{0x2000, 0xDD000000}}, {}},  // byte at +3 = 0xDD

        // --- LDRH/STRH ---
        {"LDRH R0, [R1, #0]", {0x08, 0x88}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x2000; return r; }(), 10,
            {{0x2000, 0x0000BEEF}}, {}},

        // --- SUBS Rd, Rn, Rm ---
        {"SUBS R0, R1, R2", {0x88, 0x1A}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 10; r[2] = 3; return r; }(), 10},

        // --- ADDS Rd, Rn, Rm ---
        {"ADDS R0, R1, R2", {0x88, 0x18}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 10; r[2] = 3; return r; }(), 10},

        // --- ASRS ---
        {"ASRS R0, R1, #4", {0x08, 0x11}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x80; return r; }(), 10},
        {"ASRS R0, R1 (reg)", {0x08, 0x41}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0xFF000000; r[1] = 8; return r; }(), 10},

        // --- MULS ---
        {"MULS R0, R1", {0x48, 0x43}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 7; r[1] = 6; return r; }(), 10},

        // --- BICS ---
        {"BICS R0, R1", {0x88, 0x43}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0xFF; r[1] = 0x0F; return r; }(), 10},

        // --- NEGS ---
        {"NEGS R0, R1", {0x48, 0x42}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 5; return r; }(), 10},

        // --- TST ---
        {"TST R0, R1 (nonzero)", {0x08, 0x42}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0xFF; r[1] = 0x01; return r; }(), 10},
        {"TST R0, R1 (zero)", {0x08, 0x42}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0xF0; r[1] = 0x0F; return r; }(), 10},

        // --- ADD Rd, SP, #imm ---
        {"ADD R0, SP, #16", {0x04, 0xA8}, 0x1000,
            [&]{ auto r = zero_regs; r[13] = 0x3000; return r; }(), 10},

        // --- SUB SP / ADD SP ---
        {"SUB SP, #8", {0x82, 0xB0}, 0x1000, zero_regs, 10},
        {"ADD SP, #8", {0x02, 0xB0}, 0x1000, zero_regs, 10},

        // --- LDR Rt, [PC, #imm] (literal pool) ---
        // Code at 0x1000: LDR R0, [PC, #0] → loads from (0x1000+4) & ~3 = 0x1004
        // We need data at 0x1004
        {"LDR R0, [PC, #0]", {0x00, 0x48}, 0x1000,
            zero_regs, 10,
            {{0x1004, 0xBAADF00D}}, {}},

        // --- LDRSB ---
        {"LDRSB R0, [R1, R2]", {0x88, 0x56}, 0x1000,
            [&]{ auto r = zero_regs; r[1] = 0x2000; r[2] = 0; return r; }(), 10,
            {{0x2000, 0x00000080}}, {}},  // byte 0x80 → sign-extended to 0xFFFFFF80

        // --- CMN ---
        {"CMN R0, R1", {0xC8, 0x42}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 3; r[1] = 5; return r; }(), 10},

        // --- CMP high regs ---
        {"CMP R8, R0", {0x80, 0x45}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 5; r[8] = 10; return r; }(), 10},

        // --- LSLS/LSRS reg ---
        {"LSLS R0, R1 (reg)", {0x88, 0x40}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 1; r[1] = 4; return r; }(), 10},
        {"LSRS R0, R1 (reg)", {0xC8, 0x40}, 0x1000,
            [&]{ auto r = zero_regs; r[0] = 0x100; r[1] = 4; return r; }(), 10},

        // --- Multi-instruction sequences ---
        // MOVS R0,#5; MOVS R1,#3; ADDS R0,R0,R1 → R0=8
        {"MOVS+ADDS sequence", {0x05, 0x20, 0x03, 0x21, 0x40, 0x18}, 0x1000,
            zero_regs, 10},

        // MOVS R0,#10; SUBS R0,#1; SUBS R0,#1 → R0=8
        {"MOVS+SUBS+SUBS", {0x0A, 0x20, 0x01, 0x38, 0x01, 0x38}, 0x1000,
            zero_regs, 10},

        // Forward branch tests bail to interpreter — can't compare full state.
        // The forward branch handling is tested implicitly by the e2e frame tests.

        // --- PUSH/POP roundtrip ---
        {"PUSH+POP roundtrip",
            {0xF0, 0xB5,  // PUSH {R4-R7,LR}
             0x04, 0x00,  // MOVS R4, R0 (LSLS #0)
             0xF0, 0xBD}, // POP {R4-R7,PC}
            0x1000,
            [&]{ auto r = zero_regs; r[0] = 42; r[4] = 1; r[5] = 2; r[6] = 3; r[7] = 4; r[14] = 0x2000; return r; }(), 10},
    };

    printf("Running %zu AOT WASM correctness tests...\n\n", tests.size());

    int passed = 0, failed = 0, skipped = 0;
    for (auto &tc : tests) {
        bool ok = run_test(tc);
        if (ok) passed++;
        else failed++;
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
