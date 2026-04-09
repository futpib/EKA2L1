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
                tlb_read32: Module._test_tlb_read32,
                tlb_write32: Module._test_tlb_write32,
                tlb_read8: Module._test_tlb_read8,
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
    std::uint32_t skip_reg_mask = 0;    // bitmask of register indices to skip
    // true if this test is currently expected to FAIL (documents a known
    // bug). A pass counts as an unexpected pass (also a failure from the
    // suite's POV) so fixing the bug surfaces as a test suite failure
    // until the expected_fail flag is flipped.
    bool expected_fail = false;
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
        if (tc.skip_reg_mask & (1u << i)) continue;
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

    // If the test didn't ask for specific memory addresses, scan the
    // entire 1MB sandbox for divergence so we catch any stray writes.
    if (tc.check_mem_addrs.empty()) {
        int diverged = 0;
        for (std::uint32_t addr = 0; addr + 4 <= test_mem::SIZE; addr += 4) {
            // Skip the code region (both sides wrote the same code there).
            if (addr >= tc.code_addr && addr < tc.code_addr + tc.code.size() + 2) continue;
            std::uint32_t wv = wasm_mem.read32(addr);
            std::uint32_t iv = interp_mem.read32(addr);
            if (wv != iv) {
                if (diverged < 4) {
                    printf("  FAIL %s: mem[0x%X] = 0x%08X (wasm) vs 0x%08X (interp)\n",
                        tc.name, addr, wv, iv);
                }
                diverged++;
                passed = false;
            }
        }
        if (diverged > 4) {
            printf("  FAIL %s: ... and %d more diverging words\n", tc.name, diverged - 4);
        }
    }

    // Handle expected_fail flag: a failing expected-fail test is a PASS
    // (the bug is still there). A passing expected-fail test is a FAIL
    // (someone fixed the bug and should flip the flag).
    if (tc.expected_fail) {
        if (passed) {
            printf("  FAIL %s: marked expected_fail but passes — flip the flag\n",
                tc.name);
            return false;
        }
        printf("  XFAIL %s (known failure, see test comment)\n", tc.name);
        return true;
    }

    if (passed) {
        printf("  PASS %s\n", tc.name);
    }
    return passed;
}

// ---- Main ----

// Verify that translate_thumb_block records a resume point at the address
// just after a BLX Rm. Aot_setup relies on this to register additional AOT
// entries so control can re-enter AOT after an external call returns.
static bool test_resume_points_blx_rm() {
    // MOVS R0, #1   (0x2001)
    // BLX  R3       (0x4798) — 0x47 | (0x80 | (3<<3)) = 0x47 0x98
    // MOVS R1, #2   (0x2102) — resume point at code_addr + 4
    // BX   LR       (0x4770)
    std::vector<std::uint8_t> code = {
        0x01, 0x20,       // MOVS R0, #1
        0x98, 0x47,       // BLX R3
        0x02, 0x21,       // MOVS R1, #2
        0x70, 0x47,       // BX LR
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL resume_points_blx_rm: translator produced empty body\n");
        return false;
    }
    bool found = false;
    for (auto rp : tr.resume_points) {
        if (rp == code_addr + 4) { found = true; break; }
    }
    if (!found) {
        printf("  FAIL resume_points_blx_rm: expected resume point at 0x%08X, got {",
            code_addr + 4);
        for (auto rp : tr.resume_points) printf(" 0x%08X", rp);
        printf(" }\n");
        return false;
    }
    printf("  PASS resume_points_blx_rm\n");
    return true;
}

// Verify that a non-sibling BL imm records a resume point at the address
// just after the 32-bit BL instruction.
static bool test_resume_points_bl_imm() {
    // PUSH {LR}     (0xB500) @ 0x1000
    // BL   +12      (0xF000 0xF806) @ 0x1002 — target 0x1012
    //               BL encoding: 11110 S imm10 | 11 J1 1 J2 imm11
    //               imm32 = 12 → imm11=6, imm10=0, S=0, J1=1, J2=1
    //               insn1 = 0xF000, insn2 = 0xF806
    //               next_pc = 0x1002 + 4 = 0x1006 → resume point
    // MOVS R0, #5   (0x2005) @ 0x1006
    // POP  {PC}     (0xBD00) @ 0x1008
    std::vector<std::uint8_t> code = {
        0x00, 0xB5,       // PUSH {LR}
        0x00, 0xF0,       // BL lo
        0x06, 0xF8,       // BL hi
        0x05, 0x20,       // MOVS R0, #5
        0x00, 0xBD,       // POP {PC}
    };
    std::uint32_t code_addr = 0x1000;
    // No sibling map — BL imm target is external to this block.
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr, nullptr);
    if (tr.func.body.empty()) {
        printf("  FAIL resume_points_bl_imm: translator produced empty body\n");
        return false;
    }
    bool found = false;
    for (auto rp : tr.resume_points) {
        if (rp == code_addr + 6) { found = true; break; }
    }
    if (!found) {
        printf("  FAIL resume_points_bl_imm: expected resume point at 0x%08X, got {",
            code_addr + 6);
        for (auto rp : tr.resume_points) printf(" 0x%08X", rp);
        printf(" }\n");
        return false;
    }
    printf("  PASS resume_points_bl_imm\n");
    return true;
}

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

        // Forward branch and BL bail tests can't compare full state — the AOT function
        // returns partway through, while the interpreter runs the whole function.
        // The forward branch handling is tested implicitly by the e2e frame tests.

        // --- PUSH/POP roundtrip ---
        {"PUSH+POP roundtrip",
            {0xF0, 0xB5,  // PUSH {R4-R7,LR}
             0x04, 0x00,  // MOVS R4, R0 (LSLS #0)
             0xF0, 0xBD}, // POP {R4-R7,PC}
            0x1000,
            [&]{ auto r = zero_regs; r[0] = 42; r[4] = 1; r[5] = 2; r[6] = 3; r[7] = 4; r[14] = 0x2000; return r; }(), 10},

        // --- PUSH then BL bail (ordinal 27 pattern) ---
        // PUSH {R4,LR} then BL — AOT runs PUSH, decodes BL target, sets LR/PC and bails.
        // This test only verifies PUSH pushed R4 and LR to stack. LR comparison is
        // tricky because AOT sets LR=insn_addr+4|1 while interpreter's BL jumps to
        // a random target address. We verify memory writes from PUSH only.
        {"PUSH {R4,LR} then BL bail",
            {0x10, 0xB5,  // PUSH {R4, LR}
             0x01, 0xF0, 0x7F, 0xFE}, // BL (32-bit, bails to interpreter)
            0x1000,
            [&]{ auto r = zero_regs; r[0] = 0x2000; r[4] = 0xAAAAAAAA; r[14] = 0xBBBBBBBB; return r; }(), 1,
            {}, {0x0FFF8, 0x0FFFC}, // check SP-8 and SP-4 (SP starts at 0x10000)
            (1u << 14)}, // skip LR — AOT sets it for BL target, interpreter hasn't yet

        // --- BL decoding: MOV R0,#1; BL forward ---
        // Verify the BL target decoding matches what the interpreter would compute.
        // We compare LR after both run the full BL instruction.
        // BL at 0x1002 with offset 0: target = 0x1006, LR = 0x1007 (Thumb bit set)
        // Encoding: S=0, imm10=0, J1=1, J2=1, imm11=0 → 0xF000 0xF800
        // Run 2 instructions: MOVS then BL.
        // Interpreter after BL: PC=target, LR=return_address|1
        // AOT after MOVS+BL: PC=target, LR=(insn_addr+4)|1
        // These should match.
        {"MOVS + BL offset 0",
            {0x01, 0x20,  // MOVS R0, #1
             0x00, 0xF0, 0x00, 0xF8}, // BL +0 (target = next insn)
            0x1000,
            [&]{ auto r = zero_regs; r[14] = 0xBBBBBBBB; return r; }(), 3, // 3 = MOVS + BL_1 + BL_2
            {}, {},
            0},

        // BL with small positive offset: target = 0x1006 + imm
        // Encoding for BL at 0x1002 targeting 0x1010:
        // imm32 = target - (pc+4) = 0x1010 - 0x1006 = 0xA (10)
        // Needs to encode 10/2 = 5 halfwords into imm11 (lower) or imm10 (upper):
        // imm11 = 5 (bit[11:1] = 5), imm10 = 0, S = 0, J1 = J2 = 1
        // First halfword: 0xF000 | 0 = 0xF000
        // Second halfword: 0xF800 | (J1<<13)|(J2<<11)|imm11 = 0xF800 | 5 = 0xF805
        {"MOVS + BL +10",
            {0x01, 0x20,  // MOVS R0, #1
             0x00, 0xF0, 0x05, 0xF8}, // BL target=0x1010
            0x1000,
            [&]{ auto r = zero_regs; r[14] = 0xBBBBBBBB; return r; }(), 3,
            {}, {},
            0},

        // --- ADD high reg T2 (0x4405 pattern: low Rdn, low Rm, no flags) ---
        // ADD R5, R0 — Rdn=5 (low), Rm=0 — 0x4400 | (0<<3) | 5 = 0x4405
        {"ADD R5, R0 (T2)",
            {0x05, 0x44}, 0x1000,
            [&]{ auto r = zero_regs; r[5] = 100; r[0] = 50; return r; }(), 10},
        // ADD R8, R0 — Rdn=8 (high, D=1), Rm=0 → 0x4400 | (1<<7) | (0<<3) | 0 = 0x4480
        {"ADD R8, R0 (high Rdn)",
            {0x80, 0x44}, 0x1000,
            [&]{ auto r = zero_regs; r[8] = 1000; r[0] = 500; return r; }(), 10},

        // --- CMP high reg T2 variant (0x4573 pattern: low Rn, high Rm) ---
        // CMP R3, LR — mask 0xFF00 == 0x4500, N=0, Rm=R14, Rn=R3
        {"CMP R3, LR (T2 low Rn)",
            {0x73, 0x45}, 0x1000,  // 0x4573
            [&]{ auto r = zero_regs; r[3] = 100; r[14] = 50; return r; }(), 10},

        // --- LDRSH Rt, [Rn, Rm] (0x5E00 pattern) ---
        // 0x5E00 | (Rm<<6) | (Rn<<3) | Rt
        // LDRSH R0, [R1, R2] — Rt=0, Rn=1, Rm=2 → 0x0010 | (2<<6) | (1<<3) | 0 = 0x5E88
        {"LDRSH R0, [R1, R2]",
            {0x88, 0x5E}, 0x1000,  // 0x5E88
            [&]{ auto r = zero_regs; r[1] = 0x2000; r[2] = 0; return r; }(), 10,
            {{0x2000, 0x0000FFFF}}, {}}, // halfword 0xFFFF → sign-extended to 0xFFFFFFFF

        // --- STRH Rt, [Rn, Rm] (0x5200 pattern) ---
        // STRH R0, [R1, R2] — 0x5200 | (2<<6) | (1<<3) | 0 = 0x5288
        {"STRH R0, [R1, R2]",
            {0x88, 0x52}, 0x1000,  // 0x5288
            [&]{ auto r = zero_regs; r[0] = 0xABCD; r[1] = 0x2000; r[2] = 0; return r; }(), 10,
            {}, {0x2000}},

        // --- LDRH Rt, [Rn, Rm] (0x5A00 pattern) ---
        // LDRH R0, [R1, R2] — 0x5A00 | (2<<6) | (1<<3) | 0 = 0x5A88
        {"LDRH R0, [R1, R2]",
            {0x88, 0x5A}, 0x1000,  // 0x5A88
            [&]{ auto r = zero_regs; r[1] = 0x2000; r[2] = 0; return r; }(), 10,
            {{0x2000, 0x0000ABCD}}, {}},

        // --- STR Rt, [Rn, Rm] (0x5000 pattern) ---
        // STR R0, [R1, R2] — 0x5000 | (2<<6) | (1<<3) | 0 = 0x5088
        {"STR R0, [R1, R2]",
            {0x88, 0x50}, 0x1000,  // 0x5088
            [&]{ auto r = zero_regs; r[0] = 0xDEADBEEF; r[1] = 0x2000; r[2] = 0; return r; }(), 10,
            {}, {0x2000}},

        // --- STRB Rt, [Rn, Rm] (0x5400 pattern) ---
        // STRB R0, [R1, R2] — 0x5400 | (2<<6) | (1<<3) | 0 = 0x5488
        {"STRB R0, [R1, R2]",
            {0x88, 0x54}, 0x1000,  // 0x5488
            [&]{ auto r = zero_regs; r[0] = 0xAB; r[1] = 0x2000; r[2] = 0; return r; }(), 10,
            {}, {0x2000}},

        // --- ADR Rd, label (0xA000-0xA700 pattern = ADD Rd, PC, #imm8*4) ---
        // ADR R0, #0 at 0x1000 → R0 = (PC+4)&~3 + 0 = 0x1004
        {"ADR R0, #0",
            {0x00, 0xA0}, 0x1000,  // 0xA000
            zero_regs, 10},
        // ADR R2, #20 → R2 = (PC+4)&~3 + 20 = 0x1018
        {"ADR R2, #20",
            {0x05, 0xA2}, 0x1000,  // 0xA205, imm8=5, 5*4=20
            zero_regs, 10},

        // --- STMIA Rn!, {reglist} (0xC000 pattern) ---
        // STMIA R1!, {R0, R2} — 0xC000 | (1<<8) | 0x05 = 0xC105
        {"STMIA R1!, {R0,R2}",
            {0x05, 0xC1}, 0x1000,  // 0xC105
            [&]{ auto r = zero_regs; r[0] = 0x11111111; r[1] = 0x2000; r[2] = 0x22222222; return r; }(), 10,
            {}, {0x2000, 0x2004}},

        // --- LDMIA Rn!, {reglist} (0xC800 pattern) ---
        // LDMIA R1!, {R0, R2} — 0xC800 | (1<<8) | 0x05 = 0xC905
        {"LDMIA R1!, {R0,R2}",
            {0x05, 0xC9}, 0x1000,  // 0xC905
            [&]{ auto r = zero_regs; r[1] = 0x2000; return r; }(), 10,
            {{0x2000, 0x33333333}, {0x2004, 0x44444444}}, {}},

        // --- Forward B<cond> not taken: fall-through runs ---
        // MOVS R0, #1    (0x2001) @ 0x1000
        // CMP  R0, #2    (0x2802) @ 0x1002 — Z=0, N=1 (1-2 = -1)
        // BEQ  skip      (0xD001) @ 0x1004 — offset 1 halfword: target = 0x1004+4+2 = 0x100A
        // MOVS R0, #7    (0x2007) @ 0x1006 — executes because BEQ not taken
        // BX   LR        (0x4770) @ 0x1008 — LR points to halt at 0x100C
        // skip: MOVS R0, #9 (0x2009) @ 0x100A — branch target (not reached)
        // After: R0 = 7 (interpreter runs MOVS #7 then BX LR into halt)
        // LR must have Thumb bit set; halt loop is at code_addr + code.size() = 0x100C.
        {"BEQ forward not taken",
            {0x01, 0x20,   // MOVS R0, #1
             0x02, 0x28,   // CMP R0, #2
             0x01, 0xD0,   // BEQ +2 (target 0x100A)
             0x07, 0x20,   // MOVS R0, #7
             0x70, 0x47,   // BX LR
             0x09, 0x20},  // MOVS R0, #9
            0x1000,
            [&]{ auto r = zero_regs; r[14] = 0x100D; return r; }(), 20},

        // --- Forward B<cond> taken: skip over fall-through ---
        // MOVS R0, #1    (0x2001) @ 0x1000
        // CMP  R0, #1    (0x2801) @ 0x1002 — Z=1
        // BEQ  skip      (0xD001) @ 0x1004 — target 0x100A
        // MOVS R0, #7    (0x2007) @ 0x1006 — skipped
        // BX   LR        (0x4770) @ 0x1008 — skipped
        // skip: MOVS R0, #9 (0x2009) @ 0x100A
        //       BX LR       (0x4770) @ 0x100C — LR -> halt at 0x100E
        // After: R0 = 9
        {"BEQ forward taken",
            {0x01, 0x20,   // MOVS R0, #1
             0x01, 0x28,   // CMP R0, #1
             0x01, 0xD0,   // BEQ +2 (target 0x100A)
             0x07, 0x20,   // MOVS R0, #7
             0x70, 0x47,   // BX LR
             0x09, 0x20,   // MOVS R0, #9
             0x70, 0x47},  // BX LR
            0x1000,
            [&]{ auto r = zero_regs; r[14] = 0x100F; return r; }(), 20},

        // --- Unconditional B forward ---
        // MOVS R0, #1 (0x2001) @ 0x1000
        // B    skip   (0xE001) @ 0x1002 — offset 1 halfword: target = 0x1002+4+2 = 0x1008
        // MOVS R0, #7 (0x2007) @ 0x1004 — skipped
        // BX   LR     (0x4770) @ 0x1006 — skipped
        // skip: MOVS R0, #9 (0x2009) @ 0x1008
        //       BX LR       (0x4770) @ 0x100A — LR -> halt at 0x100C
        // After: R0 = 9
        {"B forward",
            {0x01, 0x20,   // MOVS R0, #1
             0x01, 0xE0,   // B +2 (target 0x1008)
             0x07, 0x20,   // MOVS R0, #7
             0x70, 0x47,   // BX LR
             0x09, 0x20,   // MOVS R0, #9
             0x70, 0x47},  // BX LR
            0x1000,
            [&]{ auto r = zero_regs; r[14] = 0x100D; return r; }(), 20},

        // --- Backward loop ---
        // Loop target is the FIRST instruction so the current translator's
        // backward-branch handling (which jumps to the top of the WASM loop,
        // ignoring PC_IDX) works correctly. The loop head IS instruction 0.
        // loop: ADDS R0, #1 (0x3001) @ 0x1000
        //       CMP  R0, #5 (0x2805) @ 0x1002
        //       BNE  loop   (0xD1FC) @ 0x1004 — target = 0x1004+4-8 = 0x1000
        //       BX   LR     (0x4770) @ 0x1006 — LR -> halt at 0x1008
        // Init: R0 = 0. After: R0 = 5.
        {"Backward loop BNE",
            {0x01, 0x30,   // ADDS R0, #1
             0x05, 0x28,   // CMP R0, #5
             0xFC, 0xD1,   // BNE -4 (target 0x1000)
             0x70, 0x47},  // BX LR
            0x1000,
            [&]{ auto r = zero_regs; r[14] = 0x1009; return r; }(), 100},

        // --- Crash repro: AOT dispatch history entry [14] (KNOWN BUG) ---
        // entry=0x8046506E, instrs=43. The dispatch BEFORE the last one
        // in the frame-test crash history. Same rebasing convention as
        // the other crash-repros: code at 0x30000, SP rebased to fit in
        // the 1MB test memory.
        //
        // In the real frame test, this block's first instruction is a
        // BL to a sibling AOT function — the translator inlines it as a
        // direct WASM call and execution continues. In this harness we
        // translate with siblings=nullptr, so the BL bails immediately
        // and the WASM function runs ~0 instructions. The interpreter
        // still runs the full sequence via its own BL handling, and the
        // final Z flag diverges. This isn't a true minimal reproducer
        // of the crash — it's a harness limitation — but it does
        // document that AOT + interpreter produce different flag state
        // for this block when run in isolation, which is a real data
        // point for the "we return the wrong state after a short bail"
        // family of bugs.
        {"crash-repro 0x8046506E (harness limitation)",
            {0x01,0xf0, 0x29,0xfc, 0x44,0x1b, 0x20,0x1d,
             0x01,0xf0, 0xdd,0xfb, 0x00,0x1f, 0x01,0xf0,
             0x00,0xea, 0xf8,0xbd, 0x00,0x28, 0x10,0xb5,
             0x03,0xd0, 0xff,0xf7, 0xc8,0xff, 0x01,0xf0,
             0xb8,0xeb, 0x10,0xbd, 0x70,0xb5, 0x05,0x00,
             0x0e,0x00, 0x01,0xf0, 0xc4,0xfd, 0x04,0x00,
             0x30,0x00, 0x01,0xf0, 0xc0,0xfd, 0x84,0x42,
             0x01,0xd1, 0x04,0x20, 0x70,0xbd, 0x68,0x6d,
             0x73,0x6d, 0x08,0x22, 0x01,0x00, 0x11,0x40,
             0x1a,0x40, 0x91,0x42, 0x01,0xd1, 0x03,0x20,
             0x70,0xbd, 0x04,0x21, 0x08,0x40, 0x19,0x40,
             0x88,0x42, 0x01,0xd1, 0x02,0x20, 0x70,0xbd},
            0x30000,
            [&]{
                auto r = zero_regs;
                r[0]  = 0x00000000;
                r[1]  = 0x000000C7;
                r[2]  = 0x00020094; // rebased from 0x0050F794
                r[3]  = 0x000200D8; // rebased from 0x0050F7D8
                r[4]  = 0x40201018;
                r[5]  = 0x000000C7;
                r[6]  = 0x40060001;
                r[7]  = 0x00020054; // rebased from 0x0050F854
                r[13] = 0x00020080; // SP rebased from 0x0050F780
                r[14] = 0x8046506F;
                return r;
            }(),
            64, {}, {}, 0,
            /*expected_fail=*/ true},

        // --- Crash repros from the frame test AOT dispatch history ---
        // Captured after an access violation in the frame test. Each
        // entry in the AOT dispatch ring buffer becomes a test case.
        // Bytes are sliced from FntStore.dll at the entry address, and
        // the pre-dispatch register snapshot is copied verbatim. The
        // test compares the final register state after running the
        // block through both the interpreter and the WASM AOT. Any
        // divergence is the minimal reproducer for the crash.
        //
        // The harness caps code at a 1MB sandbox, so code_addr is
        // relocated to a sandbox-local address (0x10000..0x40000). SP
        // is also rebased so loads/stores land inside the sandbox. The
        // relocation is safe because the translator uses start_address
        // only for relative branch resolution and bail-PC values, and
        // the test skips PC comparison.
        //

        // --- Crash repro #1: access violation from frame test ---
        // Captured from the WASM frame test after the nested-block
        // forward-branch emitter was enabled. The interpreter crashed
        // with PC=0 at kernel.cpp:363; the last AOT dispatch before the
        // crash was this block. Bytes are sliced from FntStore.dll at
        // ROM_BASE+0x464BBE. Registers before are copied verbatim from
        // the "AOT dispatch history" dump in the crash log.
        //
        // The original PC is 0x80464BBE but the test harness caps code
        // at a 1MB sandbox; we relocate the code to 0x10000 and also
        // relocate the SP so loads/stores land inside the sandbox. LR
        // keeps its ROM value — it is only read, and any BX LR would
        // bail the AOT function and let the interpreter take over in
        // the test harness's halt-loop tail.
        {"crash-repro 0x80464BBE",
            {0xc9,0x19, 0x41,0x60, 0x28,0xe0, 0x09,0x98,
             0x00,0x28, 0x0f,0xd1, 0x01,0x22, 0x08,0xa9,
             0x09,0xa8, 0x6b,0x46, 0x07,0xc3, 0x20,0x00,
             0x0d,0x9b, 0x61,0x68, 0x0c,0x9a, 0xff,0xf7,
             0x58,0xfe, 0x09,0x98, 0x00,0x28, 0x01,0xd1,
             0x00,0x20, 0x8d,0xe7, 0x31,0x00, 0x03,0xa8,
             0x01,0xf0, 0xb9,0xff, 0x31,0x6a, 0x73,0x6a,
             0x03,0xaa, 0x00,0x91, 0x01,0x92, 0x60,0x68,
             0x0d,0x9a, 0x21,0x00, 0xff,0xf7, 0x84,0xfd,
             0x06,0x00, 0x05,0xd0, 0x02,0x00, 0x09,0x98,
             0x61,0x68, 0x08,0x9b, 0xff,0xf7, 0x3e,0xfd,
             0x07,0x99, 0x00,0x29, 0x0f,0xd0, 0x60,0x68},
            0x10000,
            [&]{
                auto r = zero_regs;
                // From the AOT dispatch history "before" snapshot.
                // SP is rebased into the test-harness sandbox.
                r[0]  = 0x00000000;
                r[1]  = 0x40060001;
                r[2]  = 0x00000200;
                r[3]  = 0x000000C7;
                r[4]  = 0x00000000;
                r[5]  = 0x00000001;
                r[6]  = 0x00000000;
                r[7]  = 0x00000000;
                r[13] = 0x00020000; // SP, rebased into the 1MB test memory
                r[14] = 0x80464CE3; // LR
                return r;
            }(),
            64,    // max_instrs: the original dispatch ran 5 ARM instrs
            {}, {},
            0},    // no register skip — we want to see any divergence

        // --- Backward branch to a non-entry instruction ---
        // Backward branches currently re-enter the WASM loop at its top
        // (instruction 0) regardless of the requested target index,
        // because the dispatch "set PC_IDX; br $loop" never actually
        // dispatches on PC_IDX. If the function is entered at insn 0 and
        // the loop head is also insn 0, the bug is invisible. When the
        // loop head is at insn > 0, execution re-runs the pre-head code
        // on every iteration.
        //
        // Layout (insn indices in parens, loop head at insn 1):
        //   @ 0x1000 (insn 0)  SUBS R0, #1               — 0x3801
        //                        decrements R0 each iter in the buggy path
        //   @ 0x1002 (insn 1)  ADDS R1, #1               — 0x3101, loop head
        //   @ 0x1004 (insn 2)  CMP  R1, #3               — 0x2903
        //   @ 0x1006 (insn 3)  BNE  target (insn 1)      — 0xD1FC
        //                        src 0x1006 PC+4 0x100A, target 0x1002,
        //                        delta -8 → imm8 -4 → 0xD1FC
        //   @ 0x1008 (insn 4)  BX LR                     — 0x4770
        //
        // Init: R0 = 10, R1 = 0.
        //
        // Correct behaviour (what the interpreter does):
        //   SUBS R0 (10→9), then loop { ADDS R1; CMP; BNE } until R1=3.
        //   Final R0=9, R1=3.
        //
        // Current broken AOT:
        //   Each BNE taken jumps to WASM loop top (insn 0), re-running
        //   SUBS R0. After 3 iterations: R0=7, R1=3.
        //   R0 differs from the interpreter's R0=9 — test FAILS, which is
        //   exactly what we want to document this bug.
        //
        // When the translator is fixed to dispatch backward branches to
        // the correct target index, this test should start PASSING.
        {"Backward branch to non-entry (KNOWN BUG)",
            {0x01, 0x38,   // SUBS R0, #1
             0x01, 0x31,   // ADDS R1, #1 (loop head, idx 1)
             0x03, 0x29,   // CMP R1, #3
             0xFC, 0xD1,   // BNE -4 (target 0x1002)
             0x70, 0x47},  // BX LR
            0x1000,
            [&]{ auto r = zero_regs; r[0] = 10; r[1] = 0; r[14] = 0x100B; return r; }(), 100,
            {}, {},
            0,
            /*expected_fail=*/ true},

        // --- Forward target reused as backward target ---
        // Target X is both a forward target (of an earlier source) and a
        // backward target (of a later source). Without the fix, the later
        // branch computes `depth = fwd_idx[X] - closed_count` which
        // underflows (fwd_idx[X] already closed) and the WASM module fails
        // to validate with an invalid branch depth.
        //
        // Layout (all at 0x1000 + offset):
        //   @ 0x1000 (insn 0)  B  middle   ; forward to 0x1006 (insn 3)
        //   @ 0x1002 (insn 1)  MOVS R1, R1 ; filler (skipped)
        //   @ 0x1004 (insn 2)  MOVS R1, R1 ; filler (skipped)
        //   @ 0x1006 (insn 3)  ADDS R0, #1 ; middle, forward target
        //   @ 0x1008 (insn 4)  CMP  R0, #3
        //   @ 0x100A (insn 5)  BNE  middle ; backward to 0x1006 (insn 3)
        //   @ 0x100C (insn 6)  BX LR       ; exit
        //
        // B  imm: source 0x1000, PC+4 = 0x1004, target 0x1006, delta 2
        //   imm11 = 1 → 0xE001
        // BNE imm: source 0x100A, PC+4 = 0x100E, target 0x1006, delta -8
        //   imm8 = -4 = 0xFC → 0xD1FC
        // MOVS R1, R1 is ADDS R1, R1, #0 (T1 encoding): 0x1C09
        //
        // Because the backward branch currently re-dispatches to WASM loop
        // top (insn 0), and insn 0 is an idempotent forward-skip to the
        // loop head, re-executing it has the same effect as jumping
        // directly to insn 3, so the loop converges.
        //
        // Init R0=0. After: R0 = 3.
        {"Forward target reused as backward",
            {0x01, 0xE0,   // B +2 (target 0x1006)
             0x09, 0x1C,   // MOVS R1, R1 (skipped)
             0x09, 0x1C,   // MOVS R1, R1 (skipped)
             0x01, 0x30,   // ADDS R0, #1 (middle, forward target)
             0x03, 0x28,   // CMP R0, #3
             0xFC, 0xD1,   // BNE -4 (target 0x1006)
             0x70, 0x47},  // BX LR
            0x1000,
            [&]{ auto r = zero_regs; r[14] = 0x100F; return r; }(), 100},
    };

    printf("Running %zu AOT WASM correctness tests...\n\n", tests.size());

    int passed = 0, failed = 0, skipped = 0;
    for (auto &tc : tests) {
        bool ok = run_test(tc);
        if (ok) passed++;
        else failed++;
    }

    // Translator-level tests that don't need a dyncom comparison.
    printf("\nRunning translator-level tests...\n\n");
    if (test_resume_points_blx_rm()) passed++; else failed++;
    if (test_resume_points_bl_imm()) passed++; else failed++;

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
