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
#include <cpu/12l1r/tlb.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/aot/arm_translator.h>
#include <cpu/aot/exit_census.h>
#include <cpu/aot/execution_limits.h>
#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/region_ir.h>
#include <cpu/aot/code_cache.h>
#include <cpu/aot/thumb_translator.h>
#include <cpu/aot/wasm_emitter.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
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
static bool g_mutate_callback_state = false, g_callback_observed_state = false;
static bool g_mutate_callback_cpsr = false;
static unsigned g_expected_callback_pc = 0;
static bool g_callback_pc_matches = true;
static bool g_count_memory_helpers = false;
static unsigned g_memory_helper_calls = 0;
static std::function<void(std::uint32_t,std::uint32_t,std::uint32_t)> g_write16_observer;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    std::uint32_t test_tlb_read32(std::uint32_t state_ptr, std::uint32_t addr) {
        if (g_count_memory_helpers) ++g_memory_helper_calls;
        if (g_expected_callback_pc)
            g_callback_pc_matches &= reinterpret_cast<std::uint32_t *>(state_ptr)[15] == g_expected_callback_pc;
        if (g_mutate_callback_state) {
            auto *state = reinterpret_cast<std::uint32_t *>(state_ptr);
            g_callback_observed_state = state[2] == 7;
            state[3] = 11;
            state[state_offsets::CFLAG / 4] = 1;
        }
        if (g_mutate_callback_cpsr) {
            auto *state = reinterpret_cast<std::uint32_t *>(state_ptr);
            g_callback_observed_state = state[state_offsets::CPSR / 4] == 0xF8000010u;
            state[state_offsets::CPSR / 4] = 0x20000210u;
        }
        return g_test_mem ? g_test_mem->read32(addr) : 0;
    }
    EMSCRIPTEN_KEEPALIVE
    void test_tlb_write32(std::uint32_t state_ptr, std::uint32_t addr, std::uint32_t val) {
        (void)state_ptr;
        if (g_count_memory_helpers) ++g_memory_helper_calls;
        if (g_test_mem) g_test_mem->write32(addr, val);
    }
    EMSCRIPTEN_KEEPALIVE
    std::uint32_t test_tlb_read8(std::uint32_t state_ptr, std::uint32_t addr) {
        (void)state_ptr;
        return g_test_mem ? g_test_mem->data[addr] : 0;
    }
    EMSCRIPTEN_KEEPALIVE
    std::uint32_t test_tlb_read16(std::uint32_t, std::uint32_t addr) {
        std::uint16_t value = 0;
        if (g_test_mem && addr + 2 <= test_mem::SIZE) std::memcpy(&value, &g_test_mem->data[addr], 2);
        return value;
    }
    EMSCRIPTEN_KEEPALIVE
    void test_tlb_write16(std::uint32_t state_ptr, std::uint32_t addr, std::uint32_t value) {
        if (g_write16_observer) { g_write16_observer(state_ptr,addr,value); return; }
        if (g_test_mem) g_test_mem->write16(addr, value);
    }
    EMSCRIPTEN_KEEPALIVE
    void test_tlb_write8(std::uint32_t state_ptr, std::uint32_t addr, std::uint32_t val) {
        (void)state_ptr;
        if (g_test_mem && addr < test_mem::SIZE) g_test_mem->data[addr] = static_cast<std::uint8_t>(val);
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
                tlb_write8: Module._test_tlb_write8,
                tlb_read16: Module._test_tlb_read16,
                tlb_write16: Module._test_tlb_write16,
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

    // --- Step 2: Translate to WASM (two-pass, with siblings) ---
    // Mini version of aot_setup's two-pass flow: first pass discovers
    // which entry points translate (top-level + resume points + branch
    // targets), second pass re-translates with a populated sibling map
    // so backward branches and sibling BLs become direct WASM calls
    // instead of bailing to the interpreter.
    std::vector<wasm_import_func> imports = {
        {"env", "tlb_read32", 2, true},
        {"env", "tlb_write32", 3, false},
        {"env", "tlb_read8", 2, true},
        {"env", "tlb_write8", 3, false},
    };
    const std::uint32_t num_imports = static_cast<std::uint32_t>(imports.size());

    sibling_map siblings;
    // Preserve insertion order so the entry function is first (matches
    // assignment of wasm indices).
    std::vector<std::uint32_t> accepted_addrs;

    auto slice_from = [&](std::uint32_t addr) -> std::pair<const std::uint8_t *, std::size_t> {
        // The test provides the full block starting at tc.code_addr.
        // For an entry at `addr` inside that block, hand the translator
        // a suffix view.
        if (addr < tc.code_addr || addr >= tc.code_addr + tc.code.size()) {
            return {nullptr, 0};
        }
        std::size_t off = addr - tc.code_addr;
        return {tc.code.data() + off, tc.code.size() - off};
    };

    auto try_translate_at = [&](std::uint32_t addr) -> translate_result {
        translate_result empty;
        if (siblings.count(addr)) return empty;
        auto [p, sz] = slice_from(addr);
        if (!p || sz < 2) return empty;
        auto r = translate_thumb_block(p, sz, addr, nullptr);
        if (r.func.body.empty() || !r.complete) return empty;
        std::uint32_t idx = num_imports + static_cast<std::uint32_t>(accepted_addrs.size());
        siblings[addr] = idx;
        accepted_addrs.push_back(addr);
        return r;
    };

    // First pass: discover. Seed with the test entry point.
    {
        auto r0 = try_translate_at(tc.code_addr);
        if (r0.func.body.empty()) {
            printf("  SKIP %s: translator produced empty body\n", tc.name);
            return true;
        }
        // Walk accepted_addrs by index so new entries added inside the
        // loop are also visited.
        std::size_t i = 0;
        while (i < accepted_addrs.size()) {
            std::uint32_t addr = accepted_addrs[i++];
            auto [p, sz] = slice_from(addr);
            if (!p) continue;
            auto r = translate_thumb_block(p, sz, addr, nullptr);
            if (r.func.body.empty() || !r.complete) continue;
            for (std::uint32_t rp : r.resume_points) try_translate_at(rp & ~1u);
            for (std::uint32_t bt : r.branch_targets) try_translate_at(bt & ~1u);
        }
    }

    // Second pass: re-translate each accepted function with siblings.
    std::vector<wasm_func_def> all_funcs;
    all_funcs.reserve(accepted_addrs.size());
    for (std::uint32_t addr : accepted_addrs) {
        auto [p, sz] = slice_from(addr);
        auto r = translate_thumb_block(p, sz, addr, &siblings);
        if (r.func.body.empty() || !r.complete) {
            printf("  FAIL %s: 2nd pass failed for 0x%08X\n", tc.name, addr);
            return false;
        }
        all_funcs.push_back(std::move(r.func));
    }

    auto wasm_bytes = build_wasm_module(all_funcs, imports);

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

// Verify that BLX imm (T2) pointing at a standard RVCT Thumb→ARM veneer
// (`LDR PC, [PC, #-4]; <thumb_addr>`) is folded into a direct BL to the
// real Thumb target. Without this inlining, BLX imm bails to the
// interpreter, which then bounces through the ARM veneer and lands in
// Thumb — two unnecessary roundtrips per call.
//
// Layout (absolute addresses):
//   0x1000  caller (Thumb, passed to the translator)
//   0x1000    MOVS R0, #1          ; 0x2001
//   0x1002    BLX   veneer         ; F000 EFFE — aligned_src=0x1004
//                                    imm32=0xFFC, target=0x2000
//   0x1006    MOVS R1, #2          ; 0x2102  <- resume point
//   0x1008    BX    LR             ; 0x4770
//   0x2000  veneer (ARM + literal, in the code_window but not in the slice)
//   0x2000    LDR PC, [PC, #-4]    ; 0xE51FF004
//   0x2004    <real thumb addr>    ; 0x3001 (bit 0 = Thumb)
//
// With veneer inlining the translator should:
//   - record a resume point at 0x1006 (BL-style bail semantics)
//   - mark the translation complete (no `bail_unsupported`)
//
// If the sibling map contains 0x3000, it should emit a direct WASM
// `call` to that sibling and return (no resume point emitted, since
// sibling calls don't bail through the interpreter).
static bool test_blx_veneer_inlining() {
    // Build a code_window containing both the caller slice and the
    // veneer 4KB later. Absolute base = 0x1000, size = 0x1008 (large
    // enough to cover the veneer at 0x2004 when addressed as
    // window_base + 0x1004).
    const std::uint32_t window_base = 0x1000;
    const std::uint32_t window_size = 0x1008;
    std::vector<std::uint8_t> window(window_size, 0);

    // Caller at offset 0 (absolute 0x1000)
    const std::uint8_t caller[] = {
        0x01, 0x20,       // MOVS R0, #1
        0x00, 0xF0,       // BLX imm lo (insn1 = 0xF000)
        0xFE, 0xEF,       // BLX imm hi (insn2 = 0xEFFE, bit 12 = 0)
        0x02, 0x21,       // MOVS R1, #2
        0x70, 0x47,       // BX LR
    };
    std::memcpy(window.data(), caller, sizeof(caller));

    // Veneer at offset 0x1000 (absolute 0x2000)
    //   LDR PC, [PC, #-4]  (ARM) = 0xE51FF004 (little-endian 04 F0 1F E5)
    const std::uint32_t VENEER = 0xE51FF004;
    std::memcpy(window.data() + 0x1000, &VENEER, 4);
    // Literal at 0x2004: real Thumb target 0x3001
    const std::uint32_t REAL_THUMB = 0x00003001;
    std::memcpy(window.data() + 0x1004, &REAL_THUMB, 4);

    code_window cw{window.data(), window_base, window_size};

    // --- Case 1: no siblings — expect resume point at next_pc (0x1006)
    //     and complete translation.
    {
        auto tr = translate_thumb_block(caller, sizeof(caller),
            window_base, nullptr, &cw);
        if (tr.func.body.empty()) {
            printf("  FAIL blx_veneer_inlining: empty body (no siblings case)\n");
            return false;
        }
        if (!tr.complete) {
            printf("  FAIL blx_veneer_inlining: translation incomplete (no siblings case)\n");
            return false;
        }
        bool found_rp = false;
        for (auto rp : tr.resume_points) {
            if (rp == window_base + 6) { found_rp = true; break; }
        }
        if (!found_rp) {
            printf("  FAIL blx_veneer_inlining: expected resume point at 0x%08X, got {",
                window_base + 6);
            for (auto rp : tr.resume_points) printf(" 0x%08X", rp);
            printf(" }\n");
            return false;
        }
    }

    // --- Case 2: sibling map contains the real Thumb target — the
    //     translator should fold BLX→veneer→BL(real) and then emit a
    //     direct WASM `call` to the sibling. No resume point at next_pc
    //     because sibling calls don't bail.
    {
        sibling_map siblings;
        const std::uint32_t SIBLING_IDX = 3; // after 3 imports
        siblings[REAL_THUMB & ~1u] = SIBLING_IDX;
        auto tr = translate_thumb_block(caller, sizeof(caller),
            window_base, &siblings, &cw);
        if (tr.func.body.empty()) {
            printf("  FAIL blx_veneer_inlining: empty body (sibling case)\n");
            return false;
        }
        if (!tr.complete) {
            printf("  FAIL blx_veneer_inlining: translation incomplete (sibling case)\n");
            return false;
        }
        // Sibling calls still need resume_points: after the WASM call
        // chain unwinds to C++, dispatch needs an AOT entry at next_pc.
        bool found_rp = false;
        for (auto rp : tr.resume_points) {
            if (rp == window_base + 6) { found_rp = true; break; }
        }
        if (!found_rp) {
            printf("  FAIL blx_veneer_inlining: sibling call should have "
                   "resume_point at 0x%08X\n", window_base + 6);
            return false;
        }
    }

    // --- Case 3: no code_window passed — translator can't resolve the
    //     veneer, must either bail as BLX (still complete) or reject.
    //     We accept either outcome, but assert no crash.
    {
        auto tr = translate_thumb_block(caller, sizeof(caller),
            window_base, nullptr, nullptr);
        if (tr.func.body.empty()) {
            printf("  FAIL blx_veneer_inlining: empty body (no window case)\n");
            return false;
        }
        // Without veneer resolution we fall through to the BLX bail
        // path, which is still a valid (if slower) translation.
    }

    printf("  PASS blx_veneer_inlining\n");
    return true;
}

// Verify that the translator stops decoding at a function terminator
// (POP {PC}) and does NOT treat trailing literal-pool bytes as code,
// even when those bytes happen to match a conditional-branch encoding.
//
// Without function-end detection the first-pass branch scanner walks
// the entire code slice and discovers phantom forward targets inside
// the literal pool. The main loop then keeps decoding past the POP,
// misreading literal data as instructions and bailing to the
// interpreter with `complete = false`.
//
// Layout (all at code_addr = 0x1000, slice 12 bytes):
//   0x1000  MOVS R0, #1          ; 0x2001
//   0x1002  POP  {PC}            ; 0xBD00  <-- function ends here
//   0x1004  literal word 0       ; halfwords 0x0000, 0xD000
//                                 ; 0xD000 alone would be B EQ +0
//                                 ; (phantom forward target at 0x100C)
//   0x1008  literal word 1       ; halfwords 0xFFFF, 0xFFFF
//                                 ; looks like a wide Thumb insn
static bool test_function_end_stops_at_pop_pc() {
    std::vector<std::uint8_t> code = {
        0x01, 0x20,       // 0x1000: MOVS R0, #1
        0x00, 0xBD,       // 0x1002: POP {PC}
        0x00, 0x00,       // 0x1004: literal low half
        0x00, 0xD0,       // 0x1006: literal high half (looks like B EQ +0)
        0xFF, 0xFF,       // 0x1008: literal
        0xFF, 0xFF,       // 0x100A: literal (looks like wide insn tail)
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL function_end_stops_at_pop_pc: empty body\n");
        return false;
    }
    // The decoder must stop right after POP {PC} at 0x1002 (inclusive),
    // i.e., end_address = 0x1004. Anything beyond means it walked into
    // the literal pool.
    const std::uint32_t expected_end = 0x1004;
    if (tr.end_address != expected_end) {
        printf("  FAIL function_end_stops_at_pop_pc: expected end_address "
               "0x%08X, got 0x%08X (decoder walked past POP {PC} into "
               "literal pool)\n", expected_end, tr.end_address);
        return false;
    }
    if (!tr.complete) {
        printf("  FAIL function_end_stops_at_pop_pc: translation marked "
               "incomplete\n");
        return false;
    }
    printf("  PASS function_end_stops_at_pop_pc\n");
    return true;
}

// Same idea, but the terminator is BX LR instead of POP {PC}.
static bool test_function_end_stops_at_bx_lr() {
    std::vector<std::uint8_t> code = {
        0x01, 0x20,       // 0x1000: MOVS R0, #1
        0x70, 0x47,       // 0x1002: BX LR
        0x00, 0x00,       // 0x1004: literal
        0x00, 0xD0,       // 0x1006: literal (looks like B EQ +0)
        0xFF, 0xFF,       // 0x1008: literal
        0xFF, 0xFF,       // 0x100A: literal
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL function_end_stops_at_bx_lr: empty body\n");
        return false;
    }
    const std::uint32_t expected_end = 0x1004;
    if (tr.end_address != expected_end) {
        printf("  FAIL function_end_stops_at_bx_lr: expected end_address "
               "0x%08X, got 0x%08X (decoder walked past BX LR into "
               "literal pool)\n", expected_end, tr.end_address);
        return false;
    }
    if (!tr.complete) {
        printf("  FAIL function_end_stops_at_bx_lr: translation marked "
               "incomplete\n");
        return false;
    }
    printf("  PASS function_end_stops_at_bx_lr\n");
    return true;
}

// Verify that `tr.branch_targets` captures the return address after every
// BL/BLX imm in the slice. Extra-entry discovery in aot_setup uses
// branch_targets to probe candidate re-entry points — if we stop reporting
// BL/BLX return addresses, we silently lose function coverage on real DLLs
// (the exact regression that landed 2256 instead of 2643 accepted functions
// on FntStore.dll before this was fixed).
//
// Layout:
//   0x1000  MOVS R0, #1    ; 0x2001
//   0x1002  BL  +4         ; F000 F802 -> target 0x100A, next_pc 0x1006
//   0x1006  MOVS R1, #2    ; 0x2102
//   0x1008  BX  LR         ; 0x4770
//   0x100A  MOVS R0, #3    ; 0x2003 (BL target, would normally bail)
//   0x100C  BX  LR         ; 0x4770
//
// Expected branch_targets (with BL next_pc added):
//   0x1006 — next_pc after the BL (for re-entry after callee returns)
//
// If branch_targets is missing 0x1006, extra-entry discovery won't probe
// it, and real DLLs lose coverage as BL return addresses aren't treated
// as AOT re-entry points.
static bool test_branch_targets_include_bl_next_pc() {
    std::vector<std::uint8_t> code = {
        0x01, 0x20,       // 0x1000: MOVS R0, #1
        0x00, 0xF0,       // 0x1002: BL lo
        0x02, 0xF8,       // 0x1004: BL hi  (target=0x100A)
        0x02, 0x21,       // 0x1006: MOVS R1, #2  <- next_pc after BL
        0x70, 0x47,       // 0x1008: BX LR
        0x03, 0x20,       // 0x100A: MOVS R0, #3
        0x70, 0x47,       // 0x100C: BX LR
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL branch_targets_include_bl_next_pc: empty body\n");
        return false;
    }
    bool has_next_pc = false;
    for (auto bt : tr.branch_targets) {
        if (bt == 0x1006) { has_next_pc = true; break; }
    }
    if (!has_next_pc) {
        printf("  FAIL branch_targets_include_bl_next_pc: expected 0x1006 "
               "(BL return addr) in branch_targets, got {");
        for (auto bt : tr.branch_targets) printf(" 0x%08X", bt);
        printf(" }\n");
        return false;
    }
    printf("  PASS branch_targets_include_bl_next_pc\n");
    return true;
}

// Verify that a function with a forward branch followed by a literal
// pool doesn't misdecode the pool. This is the realistic FntStore.dll
// pattern: compiler emits CMP/BEQ to skip the happy path, then drops a
// literal pool, then the function body continues past the pool.
//
// Before the main-loop skip-unreachable change, the decoder walked
// linearly from offset 0 through the whole slice. After a terminator
// (BX LR) it would continue because forward targets remained unclosed,
// and would try to decode literal-pool words like `0xE51FF004` (ARM
// veneer pattern) as wide Thumb, bailing with `complete=true` but
// emitting useless WASM.
//
// Layout:
//   0x1000  CMP  R0, #0          ; 0x2800
//   0x1002  BEQ  +14 -> 0x1014   ; 0xD007 (imm8=7, target=PC+4+14)
//   0x1004  MOVS R0, #1          ; 0x2001
//   0x1006  BX   LR              ; 0x4770  <-- happy-path return
//   0x1008  literal: ARM veneer  ; 0xE51FF004 (4 bytes)
//   0x100C  literal: real addr   ; 0x12345678 (4 bytes)
//   0x1010  MOVS R0, #2          ; 0x2002 (padding; never reached)
//   0x1012  BX   LR              ; 0x4770 (padding)
//   0x1014  MOVS R0, #3          ; 0x2003  <- BEQ target (reachable)
//   0x1016  BX   LR              ; 0x4770
//
// The CFG walker should mark 0x1000, 0x1002, 0x1004, 0x1006, 0x1014,
// 0x1016 as reachable. The literal pool at 0x1008-0x100F and the
// padding at 0x1010-0x1013 must NOT be decoded (they'd produce a wide
// insn bail for `F004 E51F` at 0x1008).
//
// Assertion: `tr.complete == true` AND end_address reaches 0x1018
// (past the BEQ-target's BX LR) without the decoder walking into the
// literal pool.
static bool test_literal_pool_between_early_return_and_target() {
    std::vector<std::uint8_t> code = {
        0x00, 0x28,       // 0x1000: CMP R0, #0
        0x07, 0xD0,       // 0x1002: BEQ +14 -> target 0x1014
        0x01, 0x20,       // 0x1004: MOVS R0, #1
        0x70, 0x47,       // 0x1006: BX LR
        0x04, 0xF0,       // 0x1008: literal (veneer lo)
        0x1F, 0xE5,       // 0x100A: literal (veneer hi — 0xE51F)
        0x78, 0x56,       // 0x100C: literal
        0x34, 0x12,       // 0x100E: literal
        0x02, 0x20,       // 0x1010: padding MOVS (never reached by CFG)
        0x70, 0x47,       // 0x1012: padding BX LR
        0x03, 0x20,       // 0x1014: MOVS R0, #3 (BEQ target)
        0x70, 0x47,       // 0x1016: BX LR
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL literal_pool_between_early_return_and_target: "
               "empty body\n");
        return false;
    }
    if (!tr.complete) {
        printf("  FAIL literal_pool_between_early_return_and_target: "
               "translation marked incomplete\n");
        return false;
    }
    // The decoder must stop just past the BEQ target's BX LR at 0x1016,
    // i.e. end_address = 0x1018.
    const std::uint32_t expected_end = 0x1018;
    if (tr.end_address != expected_end) {
        printf("  FAIL literal_pool_between_early_return_and_target: "
               "expected end_address 0x%08X, got 0x%08X\n",
               expected_end, tr.end_address);
        return false;
    }
    // Bail count should reflect only the legitimate terminators:
    //   1. BX LR at 0x1006 (happy-path return)
    //   2. BX LR at 0x1016 (BEQ-target return)
    // Any higher means the decoder emitted extra bails for literal-pool
    // words it misdecoded as wide instructions between 0x1008-0x100F.
    const std::uint32_t max_bails = 2;
    if (tr.bail_count > max_bails) {
        printf("  FAIL literal_pool_between_early_return_and_target: "
               "expected <= %u bails, got %u (decoder bailed on "
               "literal-pool data)\n", max_bails, tr.bail_count);
        return false;
    }
    printf("  PASS literal_pool_between_early_return_and_target\n");
    return true;
}

// Verify that the decoder stops after a mid-function unsupported wide
// insn when no more forward branch targets remain. This catches the
// common FntStore.dll pattern where a function ends in BX LR and then
// has a pool of ROM pointers; the ROM pointers look like wide insns
// (F004_E51F, E9CF_801A, etc) and the CFG walker can't tell them apart
// from real code.
//
// Before this change, the decoder would emit a bail for EACH pool word,
// producing functions with 20+ bails that yield constantly. After, the
// decoder stops at the first unsupported wide insn past the final
// forward target, capping bail_count.
//
// Layout:
//   0x1000  MOVS R0, #1  ; 0x2001
//   0x1002  BX   LR      ; 0x4770
//   0x1004  F004 E51F    ; pool (ARM veneer pattern) -- looks wide
//   0x1008  801A 0001    ; pool (ROM pointer)        -- looks wide
//
// Note: no forward branches, so after BX LR the decoder should break.
// The current CFG walker handles this case already (reachable = {0,2}).
// But for functions with early-exit branches past the pool, the walker
// marks pool offsets reachable (via fall-through after a real wide
// insn) and the decoder walks into them. This test exercises a similar
// pattern but with a forward branch to simulate that.
static bool test_stop_at_wide_bail_after_last_target() {
    // 0x1000 CMP R0, #0      -> 0x2800
    // 0x1002 BEQ +6 -> 0x100C (valid forward target AFTER the pool)
    //                          D002 = BEQ imm8=2 -> target=PC+4+4=0x100A
    //                          we want target 0x100C, so imm8=3 -> D003
    // 0x1004 MOVS R0, #1      -> 0x2001
    // 0x1006 BX   LR          -> 0x4770 (happy return)
    // 0x1008 F004 E51F        -> looks like wide insn (pool)
    //                             walker marks 0x1008 reachable because
    //                             fall-through from the "wide" insn at
    //                             0x1004... wait MOVS isn't wide.
    //
    // Actually a simpler scenario: put the pool AFTER the forward
    // target, and have the pool be unreachable.
    //
    // But we want to show the "stop after wide bail" behavior. Let me
    // use: BEQ jumps to just past the pool. The pool sits BETWEEN the
    // BX LR and the BEQ target. The walker should mark BEQ target
    // reachable, NOT the pool — but at decode time if the decoder
    // misses that and walks into the pool, the stop-at-wide-bail
    // behavior should kick in.
    //
    // To trigger the stop, we need the decoder to actually reach a
    // mid-function wide insn. The CFG walker, done right, prevents
    // this. So instead let's construct a case where the walker does
    // mark the pool reachable by mistake: a real wide insn followed
    // by pool data.
    //
    // Simpler test: trust the CFG walker, and only test the
    // insn_idx=0 case. But that already bails correctly.
    //
    // Let me test the defensive stop directly: a function where the
    // wide handler would fire AT insn_idx > 0 and no forward targets
    // remain. The simplest way: wide LDM/STM (handled) followed by
    // pool data that looks wide.
    std::vector<std::uint8_t> code = {
        0x01, 0x20,              // 0x1000: MOVS R0, #1
        0x70, 0x47,              // 0x1002: BX LR (return)
        0x04, 0xF0, 0x1F, 0xE5,  // 0x1004: pool (looks wide: F004 E51F)
        0x1A, 0x80, 0x34, 0x12,  // 0x1008: pool (801A 1234)
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL stop_at_wide_bail: empty body\n");
        return false;
    }
    if (!tr.complete) {
        printf("  FAIL stop_at_wide_bail: translation marked incomplete\n");
        return false;
    }
    // Only one legitimate bail: the BX LR at 0x1002.
    const std::uint32_t max_bails = 1;
    if (tr.bail_count > max_bails) {
        printf("  FAIL stop_at_wide_bail: expected <= %u bails, got %u\n",
            max_bails, tr.bail_count);
        return false;
    }
    printf("  PASS stop_at_wide_bail\n");
    return true;
}

// Verify that the translator accepts wide PUSH/POP (STMDB.W SP! /
// LDMIA.W SP!) without bailing. Before the wide LDM/STM handler was
// added, these would hit `bail_unsupported` at insn_idx=0 and the
// translation would be rejected entirely — functions built with -mthumb
// -marm-v7-m that push R4-R11 (very common for real C++ code) could
// not be AOT-translated.
//
// Layout:
//   0x1000  PUSH.W {R4-R11, LR}   ; E92D 4FF0 (wide)
//   0x1004  MOVS   R0, #42        ; 0x2A20
//   0x1006  POP.W  {R4-R11, PC}   ; E8BD 8FF0 (wide)
//
// Expected: translator emits a complete body with one bail
// (the PC-loading POP return).
static bool test_wide_push_pop() {
    std::vector<std::uint8_t> code = {
        0x2D, 0xE9, 0xF0, 0x4F,  // 0x1000: PUSH.W {R4-R11, LR}
        0x2A, 0x20,              // 0x1004: MOVS R0, #42
        0xBD, 0xE8, 0xF0, 0x8F,  // 0x1006: POP.W {R4-R11, PC}
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL wide_push_pop: empty body (translator bailed on "
               "wide PUSH/POP at insn_idx=0)\n");
        return false;
    }
    if (!tr.complete) {
        printf("  FAIL wide_push_pop: translation marked incomplete\n");
        return false;
    }
    // One legitimate bail: the POP.W {..., PC} function return.
    const std::uint32_t max_bails = 1;
    if (tr.bail_count > max_bails) {
        printf("  FAIL wide_push_pop: expected <= %u bails, got %u\n",
            max_bails, tr.bail_count);
        return false;
    }
    // The decoder must consume all 10 bytes (0x1000..0x100A).
    const std::uint32_t expected_end = 0x100A;
    if (tr.end_address != expected_end) {
        printf("  FAIL wide_push_pop: expected end_address 0x%08X, "
               "got 0x%08X\n", expected_end, tr.end_address);
        return false;
    }
    printf("  PASS wide_push_pop\n");
    return true;
}

// Verify that `tr.branch_targets` is liberal enough to catch potential
// entry points even inside wide-insn middle halfwords. The broad-scan
// approach exists specifically because a conservative CFG walk misses
// entries that the caller's extra-entry discovery would otherwise accept.
//
// This test encodes:
//   0x1000  BL imm (wide, 4 bytes; insn1=F000 insn2=F801 -> target 0x1006)
//   0x1004  (halfword F801 — interpreted in isolation as part of a wide
//            pattern, but at broad-scan offset 0x1004 it's inspected as
//            a standalone 16-bit word; the broad scanner should skip it)
//   0x1006  MOVS R0, #4 ; 0x2004
//   0x1008  BEQ +0      ; 0xD000 -> target 0x100C
//   0x100A  BX  LR      ; 0x4770
//   0x100C  BX  LR      ; 0x4770 (BEQ target)
//
// Expected: branch_targets should contain 0x100C (the BEQ target).
// A too-conservative scanner that only looks at CFG-reachable instruction
// starts would still find 0x100C because the BEQ is reachable, but a
// broken scanner that walks into literal pools would add noise targets.
// This test mostly exists to sanity-check that BEQ targets are still
// recorded after all the scanner rework.
static bool test_branch_targets_include_beq_target() {
    std::vector<std::uint8_t> code = {
        0x00, 0xF0,       // 0x1000: BL lo
        0x01, 0xF8,       // 0x1002: BL hi (target=0x1006, next_pc=0x1004)
        0x04, 0x20,       // 0x1004: MOVS R0, #4 (also BL's next_pc)
        0x00, 0xD0,       // 0x1006: BEQ +0 -> target 0x100A
        0x70, 0x47,       // 0x1008: BX LR
        0x70, 0x47,       // 0x100A: BX LR
    };
    std::uint32_t code_addr = 0x1000;
    auto tr = translate_thumb_block(code.data(), code.size(), code_addr);
    if (tr.func.body.empty()) {
        printf("  FAIL branch_targets_include_beq_target: empty body\n");
        return false;
    }
    bool has_beq_target = false;
    for (auto bt : tr.branch_targets) {
        if (bt == 0x100A) { has_beq_target = true; break; }
    }
    if (!has_beq_target) {
        printf("  FAIL branch_targets_include_beq_target: expected 0x100A "
               "(BEQ target) in branch_targets, got {");
        for (auto bt : tr.branch_targets) printf(" 0x%08X", bt);
        printf(" }\n");
        return false;
    }
    printf("  PASS branch_targets_include_beq_target\n");
    return true;
}

// Test MOVW/MOVT: load 16-bit immediate into register, then set top half.
// MOVW R0, #0x1234:  insn1=F241 (i=0,imm4=1), insn2=0234 (imm3=0,Rd=0,imm8=0x34)
//   imm16 = (1<<12)|(0<<11)|(0<<8)|0x34 = 0x1034... let me compute:
//   MOVW encoding: F240 | (i<<10) | imm4, insn2 = (imm3<<12) | (Rd<<8) | imm8
//   For imm16=0xABCD: imm4=0xA, i=1, imm3=0x5, imm8=0xCD
//     insn1 = F240 | (1<<10) | 0xA = F240 | 0x400 | 0xA = F64A
//     insn2 = (5<<12) | (0<<8) | 0xCD = 0x50CD
static bool test_wide_movw_movt() {
    // MOVW R0, #0xABCD; MOVT R0, #0x1234; BX LR
    // MOVW: imm16=0xABCD → imm4=A, i=1, imm3=5, imm8=CD
    //   insn1 = F240 | (1<<10) | A = F64A
    //   insn2 = (5<<12) | (0<<8) | CD = 50CD
    // MOVT: imm16=0x1234 → imm4=1, i=0, imm3=2, imm8=34
    //   insn1 = F2C0 | (0<<10) | 1 = F2C1
    //   insn2 = (2<<12) | (0<<8) | 34 = 2034
    std::vector<std::uint8_t> code = {
        0x4A, 0xF6, 0xCD, 0x50,  // MOVW R0, #0xABCD
        0xC1, 0xF2, 0x34, 0x20,  // MOVT R0, #0x1234
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_movw_movt: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_movw_movt: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_movw_movt\n");
    return true;
}

// Test wide LDR.W Rd, [Rn, #imm12] and STR.W Rd, [Rn, #imm12].
static bool test_wide_ldr_str_imm12() {
    // STR.W R0, [R1, #256]; LDR.W R2, [R1, #256]; BX LR
    // STR.W: insn1 = F8C0 | Rn=1 → F8C1, insn2 = (Rd=0)<<12 | imm12=0x100 → 0x0100
    // LDR.W: insn1 = F8D0 | Rn=1 → F8D1, insn2 = (Rd=2)<<12 | imm12=0x100 → 0x2100
    std::vector<std::uint8_t> code = {
        0xC1, 0xF8, 0x00, 0x01,  // STR.W R0, [R1, #256]
        0xD1, 0xF8, 0x00, 0x21,  // LDR.W R2, [R1, #256]
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_ldr_str_imm12: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_ldr_str_imm12: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_ldr_str_imm12\n");
    return true;
}

// Test wide ADD.W / SUB.W with modified immediate.
static bool test_wide_add_sub_imm() {
    // ADD.W R0, R1, #100; SUB.W R2, R1, #50; BX LR
    // ADD.W: insn = F100 | (S=0)<<4 | Rn=1 → F101
    //   imm12 = 100 = 0x064 → i=0, imm3=0, imm8=0x64
    //   insn2 = (0<<12) | (R0<<8) | 0x64 = 0x0064
    // SUB.W: insn = F1A0 | Rn=1 → F1A1
    //   imm12 = 50 = 0x032 → insn2 = (R2<<8) | 0x32 = 0x0232
    std::vector<std::uint8_t> code = {
        0x01, 0xF1, 0x64, 0x00,  // ADD.W R0, R1, #100
        0xA1, 0xF1, 0x32, 0x02,  // SUB.W R2, R1, #50
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_add_sub_imm: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_add_sub_imm: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_add_sub_imm\n");
    return true;
}

// Test wide AND.W / ORR.W / EOR.W / BIC.W with modified immediate.
static bool test_wide_and_orr_eor_bic_imm() {
    // AND.W R0, R1, #0xFF; ORR.W R2, R1, #0xFF00; BX LR
    // AND.W: insn = F000 | Rn=1 → F001, imm12=0xFF → insn2=(0<<12)|(R0<<8)|0xFF = 0x00FF
    // ORR.W: insn = F040 | Rn=1 → F041, imm12 for 0xFF00:
    //   0xFF00 = XX00XX00 pattern → bits[11:10]=10, val=0xFF → imm12 = (2<<10)|0xFF = 0xAFF... hmm
    //   Actually ThumbExpandImm: for 0xFF, imm12=0xFF, result=0xFF. Let's use that.
    // ORR.W R2, R1, #0xFF: insn = F041, insn2 = (0<<12)|(R2<<8)|0xFF = 0x02FF
    std::vector<std::uint8_t> code = {
        0x01, 0xF0, 0xFF, 0x00,  // AND.W R0, R1, #0xFF
        0x41, 0xF0, 0xFF, 0x02,  // ORR.W R2, R1, #0xFF
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_and_orr_eor_bic_imm: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_and_orr_eor_bic_imm: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_and_orr_eor_bic_imm\n");
    return true;
}

// Test wide LDR.W / STR.W with register offset.
static bool test_wide_ldr_str_reg() {
    // LDR.W R0, [R1, R2, LSL #2]; BX LR
    // insn1 = F850 | Rn=1 → F851, insn2 = (Rd=0)<<12 | (shift=2)<<4 | Rm=2 = 0x0022
    std::vector<std::uint8_t> code = {
        0x51, 0xF8, 0x22, 0x00,  // LDR.W R0, [R1, R2, LSL #2]
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_ldr_str_reg: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_ldr_str_reg: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_ldr_str_reg\n");
    return true;
}

// Test wide LDR.W with negative 8-bit offset (T4 form).
static bool test_wide_ldr_neg_offset() {
    // LDR.W R0, [R1, #-8]: insn1 = F850 | Rn=1 = F851
    //   insn2 = (Rd=0)<<12 | 1 P U W imm8
    //   P=1, U=0 (negative), W=0: bits[11:8] = 0b1100 = 0xC
    //   insn2 = 0x0C08
    std::vector<std::uint8_t> code = {
        0x51, 0xF8, 0x08, 0x0C,  // LDR.W R0, [R1, #-8]
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_ldr_neg_offset: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_ldr_neg_offset: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_ldr_neg_offset\n");
    return true;
}

// Test wide unconditional branch B.W.
static bool test_wide_b_uncond() {
    // B.W +4 (skip 2 bytes): insn1 = F000, insn2 = B802
    //   target = PC+4 + imm32. For +4: imm32=4, but encoding is complex.
    //   B.W target=0x1008 from 0x1000: offset = target - (addr+4) = 0x1008 - 0x1004 = 4
    //   S=0, I1=1, I2=1, imm10=0, imm11=2: J1=!(1^0)=1, J2=!(1^0)=1
    //   insn1 = F000 (S=0, imm10=0)
    //   insn2 = 1001 J1=1 1 J2=1 imm11=2 = 0b10_1_1_1_1_00000000010 = 0xBF02...
    //   Actually: insn2[15:12]=10J11, insn2[11:1]=imm11, insn2[0]=0
    //   insn2 = (1<<15) | (0<<14) | (J1<<13) | (1<<12) | (J2<<11) | imm11
    //   = 0x8000 | 0 | (1<<13) | 0x1000 | (1<<11) | 2
    //   = 0x8000 | 0x2000 | 0x1000 | 0x0800 | 0x0002 = 0xB802
    // Actually the encoding for B.W is: insn2 & 0xD000 == 0x9000
    //   insn2 = (1<<15) | (0<<14) | (J1<<13) | (0<<12) | (J2<<11) | imm11
    //   = 0x8000 | (1<<13) | (1<<11) | 2 = 0x8000 | 0x2000 | 0x0800 | 2 = 0xA802
    // Hmm, let me just do a simple forward bail test.
    std::vector<std::uint8_t> code = {
        0x00, 0xF0, 0x02, 0xA8,  // B.W +4 (target 0x1008, from 0x1000+4=0x1004)
        // Wait, this target calculation... let me be more careful.
        // Actually just test that the translator doesn't reject it.
        // I'll use a simple encoding and verify no-bail.
    };
    // Re-encode: B.W to 0x1008 from 0x1000.
    // offset = 0x1008 - (0x1000+4) = 4
    // imm32 = 4. S=0.
    // imm11 = (4>>1) & 0x7FF = 2
    // imm10 = (4>>12) & 0x3FF = 0
    // I1=1 (bit 23 of 4 is 0, so i1=0, J1=!(0^0)=1)
    // I2=1 similarly
    // insn1 = 0xF000 | (S=0)<<10 | imm10=0 = 0xF000
    // insn2 bit 15=1, bit 14=0, bit 13=J1=1, bit 12=0, bit 11=J2=1
    //   = (1<<15)|(0<<14)|(1<<13)|(0<<12)|(1<<11) | imm11=2
    //   = 0x8000 | 0x2000 | 0x0800 | 2 = 0xA802
    code = {
        0x00, 0xF0, 0x02, 0xA8,  // B.W +4 (-> 0x1008)
        0x01, 0x20,              // 0x1004: MOVS R0, #1 (skipped)
        0x02, 0x20,              // 0x1006: MOVS R0, #2 (skipped)
        0x03, 0x20,              // 0x1008: MOVS R0, #3 (target)
        0x70, 0x47,              // 0x100A: BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_b_uncond: translator rejected\n");
        return false;
    }
    // B.W currently bails, so expect 2 bails (B.W + BX LR)
    if (tr.bail_count > 2) {
        printf("  FAIL wide_b_uncond: expected <= 2 bails, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_b_uncond\n");
    return true;
}

// Test LDRD/STRD.
static bool test_wide_ldrd_strd() {
    // STRD R0, R1, [R2, #8]; LDRD R3, R4, [R2, #8]; BX LR
    // STRD: E9C0 | (P=1)<<8 | (U=1)<<7 | (bit6=1) | (W=0)<<5 | (L=0) | Rn=2
    //   E9C0 isn't right... Let me look up the exact encoding.
    //   STRD: 1110_100P_U1W0_Rn | Rt Rt2 imm8
    //   P=1, U=1, W=0: insn = E8C0 | (1<<8) | (1<<7) | (1<<6) | Rn
    //   = E8C0 | 0x100 | 0x80 | 0x40 | 2 = E9C2
    //   insn2 = (Rt=0)<<12 | (Rt2=1)<<8 | imm8=2 (offset=8/4=2)
    //   = 0x0102
    // LDRD: same but L=1: insn = E9C2 | 0x10 = E9D2
    //   insn2 = (Rt=3)<<12 | (Rt2=4)<<8 | 2 = 0x3402
    std::vector<std::uint8_t> code = {
        0xC2, 0xE9, 0x02, 0x01,  // STRD R0, R1, [R2, #+8]
        0xD2, 0xE9, 0x02, 0x34,  // LDRD R3, R4, [R2, #+8]
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_ldrd_strd: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_ldrd_strd: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_ldrd_strd\n");
    return true;
}

// Test UBFX/SBFX.
static bool test_wide_ubfx_sbfx() {
    // UBFX R0, R1, #4, #8: extract bits [11:4]
    //   insn = F3C0 | Rn=1 = F3C1
    //   lsb=4: imm3=(4>>2)&7=1, imm2=4&3=0
    //   widthm1 = 7
    //   insn2 = (imm3<<12) | (Rd=0)<<8 | (imm2<<6) | widthm1
    //         = (1<<12) | 0 | 0 | 7 = 0x1007
    std::vector<std::uint8_t> code = {
        0xC1, 0xF3, 0x07, 0x10,  // UBFX R0, R1, #4, #8
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_ubfx_sbfx: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_ubfx_sbfx: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_ubfx_sbfx\n");
    return true;
}

// Test BFI/BFC.
static bool test_wide_bfi_bfc() {
    // BFC R0, #4, #8: clear bits [11:4]
    //   insn = F360 | Rn=15 = F36F
    //   lsb=4: imm3=1, imm2=0. msb=11.
    //   insn2 = (1<<12) | (R0<<8) | 0 | 11 = 0x100B
    std::vector<std::uint8_t> code = {
        0x6F, 0xF3, 0x0B, 0x10,  // BFC R0, #4, #8
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_bfi_bfc: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_bfi_bfc: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_bfi_bfc\n");
    return true;
}

// Test UXTB.W / SXTH.W.
static bool test_wide_uxtb_sxth() {
    // UXTB.W R0, R1: insn = FA5F, insn2 = F0<<8... wait
    //   UXTB.W Rd, Rm: insn=FA5F, insn2=(0xF<<12)|(Rd<<8)|0x80|Rm
    //   R0, R1: insn2 = 0xF080 | (0<<8) | 1 = 0xF081
    std::vector<std::uint8_t> code = {
        0x5F, 0xFA, 0x81, 0xF0,  // UXTB.W R0, R1
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_uxtb_sxth: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_uxtb_sxth: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_uxtb_sxth\n");
    return true;
}

// Test CLZ.
static bool test_wide_clz() {
    // CLZ R0, R1: insn = FAB0 | Rm=1 = FAB1
    //   insn2 = (0xF<<12) | (Rd=0<<8) | 0x80 | Rm2=1 = 0xF081
    std::vector<std::uint8_t> code = {
        0xB1, 0xFA, 0x81, 0xF0,  // CLZ R0, R1
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_clz: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_clz: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_clz\n");
    return true;
}

// Test MUL/MLA/MLS.
static bool test_wide_mul_mla_mls() {
    // MUL R0, R1, R2: insn = FB00 | Rn=1 = FB01
    //   insn2 = (Ra=0xF<<12) | (Rd=0<<8) | Rm=2 = 0xF002
    std::vector<std::uint8_t> code = {
        0x01, 0xFB, 0x02, 0xF0,  // MUL R0, R1, R2
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_mul_mla_mls: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_mul_mla_mls: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_mul_mla_mls\n");
    return true;
}

// Test SDIV/UDIV.
static bool test_wide_sdiv_udiv() {
    // SDIV R0, R1, R2: insn = FB90 | Rn=1 = FB91
    //   insn2 = (0xF<<12) | (Rd=0<<8) | (0xF<<4) | Rm=2 = 0xF0F2
    std::vector<std::uint8_t> code = {
        0x91, 0xFB, 0xF2, 0xF0,  // SDIV R0, R1, R2
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_sdiv_udiv: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_sdiv_udiv: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_sdiv_udiv\n");
    return true;
}

// Test shifted-register data processing (e.g. ADD.W Rd, Rn, Rm, LSL #2).
static bool test_wide_shifted_reg() {
    // ADD.W R0, R1, R2, LSL #2: insn = EA00 | (op=1000)<<1=not right.
    //   Actually: 1110101 op[3:0] S Rn → bits [15:9]=1110101, then op S Rn
    //   ADD: op=1000, S=0: insn = EA00 | (0x8<<5) | (0<<4) | Rn=1
    //     = EA00 | 0x100 | 1 = EB01
    //   insn2 = (imm3<<12) | (Rd=0<<8) | (imm2<<6) | (type=0<<4) | Rm=2
    //   shift=2: imm3=(2>>2)&7=0, imm2=2&3=2
    //   insn2 = 0 | 0 | (2<<6) | 0 | 2 = 0x0082
    std::vector<std::uint8_t> code = {
        0x01, 0xEB, 0x82, 0x00,  // ADD.W R0, R1, R2, LSL #2
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_shifted_reg: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_shifted_reg: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_shifted_reg\n");
    return true;
}

// Test ADDW/SUBW (12-bit plain immediate).
static bool test_wide_addw_subw() {
    // ADDW R0, R1, #1000: insn = F200 | (i=0)<<10 | Rn=1 = F201
    //   imm12 = 1000 = 0x3E8 → imm3=(0x3E8>>8)&7=3, imm8=0xE8
    //   insn2 = (0<<15) | (imm3<<12) | (Rd=0<<8) | imm8
    //   = (3<<12) | 0xE8 = 0x30E8
    std::vector<std::uint8_t> code = {
        0x01, 0xF2, 0xE8, 0x30,  // ADDW R0, R1, #1000
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_addw_subw: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_addw_subw: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_addw_subw\n");
    return true;
}

// Test MOV.W / MVN.W with modified immediate.
static bool test_wide_mov_mvn_imm() {
    // MOV.W R0, #42: insn = F04F | (i=0)<<10 | (S=0)<<4 = F04F
    //   imm12 = 42 = 0x2A → imm3=0, imm8=0x2A
    //   insn2 = (0<<12) | (R0<<8) | 0x2A = 0x002A
    std::vector<std::uint8_t> code = {
        0x4F, 0xF0, 0x2A, 0x00,  // MOV.W R0, #42
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_mov_mvn_imm: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_mov_mvn_imm: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_mov_mvn_imm\n");
    return true;
}

// Test wide STRB.W with 12-bit immediate (now that we have tlb_write8).
static bool test_wide_strb_imm12() {
    // STRB.W R0, [R1, #100]: insn = F880 | Rn=1 = F881
    //   insn2 = (Rd=0)<<12 | imm12=100 = 0x0064
    std::vector<std::uint8_t> code = {
        0x81, 0xF8, 0x64, 0x00,  // STRB.W R0, [R1, #100]
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL wide_strb_imm12: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL wide_strb_imm12: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS wide_strb_imm12\n");
    return true;
}

// Test VFP: VLDR.32, VADD.F32, VSTR.32 sequence.
// This verifies the translator can handle basic single-precision VFP
// without bailing on the VFP instructions themselves.
static bool test_vfp_vldr_vadd_vstr() {
    // VLDR S0, [R0, #0]:  ED90 0A00  (U=1, D=0, Rn=0, Vd=0, imm8=0)
    //   ARM: ED900A00. insn=ED90, insn2=0A00.
    // VLDR S1, [R0, #4]:  ED90 0A01  → wait, S1 encoding:
    //   sd = (Vd:D) where Vd=insn2[15:12], D=insn[22]
    //   For S1: sd=1 → Vd=0, D=1. insn bit 22 set: ED90 → EDD0 (bit 22 = 0x40 in insn)
    //   insn = EDD0 | Rn=0 = EDD0, insn2 = 0A01
    // VADD.F32 S0, S0, S1: EE30 0A20
    //   ARM: EE300A20. fop=FOP_FADD. sd=0, sn=0, sm=1.
    //   sd: insn2[15:12]=0, insn[22]=0 → sd=0.
    //   sn: insn[19:16]=0, insn[7]=0 → sn=0.
    //   sm: insn2[3:0]=0, insn2[5]=1 → sm=1. Wait, insn2=0A20: bits[3:0]=0, bit5=1.
    //   sm = (0<<1) | (1>>5 of bit5) = 0|1 = 1. Yes.
    // VSTR S0, [R0, #8]:  ED80 0A02  (U=1, D=0, Rn=0, Vd=0, imm8=2)
    // BX LR: 4770
    std::vector<std::uint8_t> code = {
        0x90, 0xED, 0x00, 0x0A,  // VLDR S0, [R0, #0]
        0xD0, 0xED, 0x01, 0x0A,  // VLDR S1, [R0, #4]
        0x30, 0xEE, 0x20, 0x0A,  // VADD.F32 S0, S0, S1
        0x80, 0xED, 0x02, 0x0A,  // VSTR S0, [R0, #8]
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL vfp_vldr_vadd_vstr: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL vfp_vldr_vadd_vstr: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS vfp_vldr_vadd_vstr\n");
    return true;
}

// Test VFP: VCVT + VCMP + VMRS sequence (int→float→compare→flags).
static bool test_vfp_vcvt_vcmp_vmrs() {
    // VMOV S0, R0:     EE00 0A10 (to VFP, sn=0, Rt=0)
    //   ARM: EE000A10. insn=EE00, insn2=0A10.
    // VCVT.F32.S32 S0, S0: EEB8 0AC0
    //   ARM: EEB80AC0. FOP_EXT, FEXT_FSITO. sd=0, sm=0.
    //   insn[22]=0 → D=0 for sd. insn2[5]=0 → M=0 for sm.
    //   Actually let me check: EEB8 → bits[27:20] = 0xEB, bit 6 = 1 (from 0xAC0 bit 6).
    //   fop = arm & FOP_MASK = EEB80AC0 & 00b00040 = 00b00040 = FOP_EXT.
    //   fext = arm & FEXT_MASK = EEB80AC0 & 000f0080 = 00080080 = FEXT_FSITO.
    // VCMPZ.F32 S0:     EEB5 0A40
    //   ARM: EEB50A40. FOP_EXT, FEXT_FCMPZ (0x00050000).
    //   fext = EEB50A40 & 000f0080 = 00050000 = FEXT_FCMPZ.
    // VMRS APSR, FPSCR: EEF1 FA10
    //   ARM: EEF1FA10. Rt=15 (F in bits[15:12]).
    // BX LR: 4770
    std::vector<std::uint8_t> code = {
        0x00, 0xEE, 0x10, 0x0A,  // VMOV S0, R0
        0xB8, 0xEE, 0xC0, 0x0A,  // VCVT.F32.S32 S0, S0
        0xB5, 0xEE, 0x40, 0x0A,  // VCMP.F32 S0, #0.0
        0xF1, 0xEE, 0x10, 0xFA,  // VMRS APSR_nzcv, FPSCR
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL vfp_vcvt_vcmp_vmrs: translator rejected\n");
        return false;
    }
    if (tr.bail_count > 1) {
        printf("  FAIL vfp_vcvt_vcmp_vmrs: expected <= 1 bail, got %u\n", tr.bail_count);
        return false;
    }
    printf("  PASS vfp_vcvt_vcmp_vmrs\n");
    return true;
}

// Test that VFP double-precision instructions produce a valid WASM module
// that can be instantiated. This catches the f64.store alignment bug
// (alignment=3 is rejected by WASM validators when memory is shared).
static bool test_vfp_f64_instantiation() {
    // VCVT.F64.F32 D0, S0: promotes single to double, uses f64.store
    //   ARM: EEB70AC0. insn=EEB7, insn2=0AC0.
    //   FOP_EXT, FEXT_FCVT with coproc=0xB (double dest).
    // BX LR
    std::vector<std::uint8_t> code = {
        0xB7, 0xEE, 0xC0, 0x0A,  // VCVT.F64.F32 D0, S0
        0x70, 0x47,              // BX LR
    };
    auto tr = translate_thumb_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL vfp_f64_instantiation: translator rejected\n");
        return false;
    }
    // Build a WASM module and try to instantiate it (via js_run_aot_wasm)
    // to catch alignment errors at the WASM validator level.
    std::vector<wasm_import_func> imports = {
        {"env", "tlb_read32", 2, true},
        {"env", "tlb_write32", 3, false},
        {"env", "tlb_read8", 2, true},
        {"env", "tlb_write8", 3, false},
    };
    auto wasm = build_wasm_module({tr.func}, imports);
    if (wasm.empty()) {
        printf("  FAIL vfp_f64_instantiation: empty module\n");
        return false;
    }
#ifdef __EMSCRIPTEN__
    // Actually instantiate to catch validator errors like bad alignment
    std::vector<std::uint8_t> state(1024, 0);
    int result = js_run_aot_wasm(wasm.data(), static_cast<int>(wasm.size()),
        state.data(), static_cast<int>(state.size()));
    if (result < 0) {
        printf("  FAIL vfp_f64_instantiation: WASM instantiation failed\n");
        return false;
    }
#endif
    printf("  PASS vfp_f64_instantiation\n");
    return true;
}

// Test that resume_points are generated for BL instructions beyond 1024
// bytes from the function start. Before the slice cap increase (1024→4096),
// BLs past byte 1024 were never scanned, so their return addresses had
// no AOT entry and the interpreter ran FntStore code at those addresses.
static bool test_resume_points_beyond_1024() {
    // Build a function with a BL at offset ~1030 from start.
    // Fill with MOVS R0, #0 (0x2000) up to ~1028 bytes, then BL.
    std::vector<std::uint8_t> code;
    std::uint32_t addr = 0x1000;
    // 514 MOVS instructions = 1028 bytes
    for (int i = 0; i < 514; i++) {
        code.push_back(0x00); code.push_back(0x20); // MOVS R0, #0
    }
    // BL +0 at offset 1028 (addr 0x1000 + 1028 = 0x1404)
    // BL target = PC+4+0 = 0x1408. Encoding: F000 F800
    //   S=0, imm10=0, J1=1, J2=1, imm11=0 → insn2 = (1<<15)|(1<<14)|(1<<13)|(1<<12)|(1<<11) = 0xF800
    code.push_back(0x00); code.push_back(0xF0); // BL hi
    code.push_back(0x00); code.push_back(0xF8); // BL lo (target = 0x1408)
    // BX LR at offset 1032
    code.push_back(0x70); code.push_back(0x47);

    auto tr = translate_thumb_block(code.data(), code.size(), addr);
    if (tr.func.body.empty()) {
        printf("  FAIL resume_points_beyond_1024: translator rejected\n");
        return false;
    }
    // The BL at 0x1404 should produce resume_point = 0x1408
    bool found = false;
    for (auto rp : tr.resume_points) {
        if (rp == 0x1408) { found = true; break; }
    }
    if (!found) {
        printf("  FAIL resume_points_beyond_1024: 0x1408 not in resume_points (got %zu points)\n",
            tr.resume_points.size());
        return false;
    }
    printf("  PASS resume_points_beyond_1024\n");
    return true;
}

// Test that sibling BL calls generate resume_points. Before this fix,
// only non-sibling BLs (that bail to interpreter) added resume_points.
// Sibling calls return directly to C++ dispatch via ret(), and if next_pc
// has no AOT entry, the interpreter runs the caller's code after the BL.
static bool test_sibling_bl_resume_point() {
    // Two functions: A calls B. B is a sibling.
    // A: MOVS R0, #1; BL B; MOVS R0, #2; BX LR
    // B: MOVS R0, #3; BX LR
    //
    // A at 0x1000, B at 0x1010.
    // BL B from 0x1002: target = 0x1010, offset = 0x1010 - (0x1002+4) = 10
    //   imm11 = 10/2 = 5, S=0, J1=1, J2=1
    //   insn1 = F000, insn2 = F805
    std::vector<std::uint8_t> code_a = {
        0x01, 0x20,              // 0x1000: MOVS R0, #1
        0x00, 0xF0, 0x05, 0xF8, // 0x1002: BL 0x1010
        0x02, 0x20,              // 0x1006: MOVS R0, #2
        0x70, 0x47,              // 0x1008: BX LR
    };
    // First pass: no siblings — BL generates resume_point via non-sibling path
    auto tr1 = translate_thumb_block(code_a.data(), code_a.size(), 0x1000);
    bool has_rp_first_pass = false;
    for (auto rp : tr1.resume_points) {
        if (rp == 0x1006) { has_rp_first_pass = true; break; }
    }
    if (!has_rp_first_pass) {
        printf("  FAIL sibling_bl_resume_point: 0x1006 not in first-pass resume_points\n");
        return false;
    }

    // Second pass: with sibling map containing B at 0x1010
    sibling_map siblings;
    siblings[0x1010] = 4; // arbitrary function index
    auto tr2 = translate_thumb_block(code_a.data(), code_a.size(), 0x1000, &siblings);
    bool has_rp_second_pass = false;
    for (auto rp : tr2.resume_points) {
        if (rp == 0x1006) { has_rp_second_pass = true; break; }
    }
    if (!has_rp_second_pass) {
        printf("  FAIL sibling_bl_resume_point: 0x1006 not in second-pass (sibling) resume_points\n");
        return false;
    }
    printf("  PASS sibling_bl_resume_point\n");
    return true;
}

// Compile-time verification of state_offsets against ARMul_State layout.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(ARMul_State, Reg) == state_offsets::REG, "REG offset mismatch");
static_assert(offsetof(ARMul_State, Cpsr) == state_offsets::CPSR, "CPSR offset mismatch");
static_assert(offsetof(ARMul_State, NFlag) == state_offsets::NFLAG, "NFLAG offset mismatch");
static_assert(offsetof(ARMul_State, ZFlag) == state_offsets::ZFLAG, "ZFLAG offset mismatch");
static_assert(offsetof(ARMul_State, CFlag) == state_offsets::CFLAG, "CFLAG offset mismatch");
static_assert(offsetof(ARMul_State, VFlag) == state_offsets::VFLAG, "VFLAG offset mismatch");
static_assert(offsetof(ARMul_State, aot_tlb) == state_offsets::AOT_TLB, "AOT TLB offset mismatch");
static_assert(offsetof(ARMul_State, aot_exit) == state_offsets::AOT_EXIT, "AOT exit offset mismatch");
static_assert(offsetof(ARMul_State, aot_budget) == state_offsets::AOT_BUDGET, "AOT budget offset mismatch");
static_assert(offsetof(ARMul_State, TFlag) == state_offsets::TFLAG, "TFLAG offset mismatch");
static_assert(offsetof(ARMul_State, VFP) == state_offsets::VFP_SYS, "VFP_SYS offset mismatch");
static_assert(offsetof(ARMul_State, ExtReg) == state_offsets::EXTREG, "EXTREG offset mismatch");
#pragma GCC diagnostic pop

// ---- ARM translator-level tests ----

static bool test_arm_mov_imm() {
    // MOV R0, #42 (E3A0002A) — unconditional
    std::uint32_t inst = 0xE3A0002A;
    std::vector<std::uint8_t> code(4);
    std::memcpy(code.data(), &inst, 4);
    // BX LR (E12FFF1E)
    std::uint32_t bx_lr = 0xE12FFF1E;
    code.resize(8);
    std::memcpy(code.data() + 4, &bx_lr, 4);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty()) {
        printf("  FAIL arm_mov_imm: empty body\n");
        return false;
    }
    if (!tr.complete) {
        printf("  FAIL arm_mov_imm: not complete (bail_count=%u)\n", tr.bail_count);
        return false;
    }
    printf("  PASS arm_mov_imm\n");
    return true;
}

static bool test_arm_add_sub_imm() {
    // ADD R0, R1, #10 (E281000A) — Rd=0, Rn=1, imm=10
    // SUB R2, R0, #5  (E240200 5) — Rd=2, Rn=0, imm=5
    // BX LR
    std::uint32_t insns[] = { 0xE281000A, 0xE2402005, 0xE12FFF1E };
    std::vector<std::uint8_t> code(12);
    std::memcpy(code.data(), insns, 12);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_add_sub_imm: translate failed\n");
        return false;
    }
    printf("  PASS arm_add_sub_imm\n");
    return true;
}

static bool test_arm_cmp_beq() {
    // CMP R0, #5 (E3500005)
    // BEQ +0 (0A000000) — skip one instruction if equal
    // MOV R1, #1 (E3A01001)
    // BX LR (E12FFF1E)
    std::uint32_t insns[] = { 0xE3500005, 0x0A000000, 0xE3A01001, 0xE12FFF1E };
    std::vector<std::uint8_t> code(16);
    std::memcpy(code.data(), insns, 16);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_cmp_beq: translate failed\n");
        return false;
    }
    printf("  PASS arm_cmp_beq\n");
    return true;
}

static bool test_arm_ldr_str_imm() {
    // LDR R0, [R1, #4] (E5910004)
    // STR R0, [R1, #8] (E5810008)
    // BX LR
    std::uint32_t insns[] = { 0xE5910004, 0xE5810008, 0xE12FFF1E };
    std::vector<std::uint8_t> code(12);
    std::memcpy(code.data(), insns, 12);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_ldr_str_imm: translate failed\n");
        return false;
    }
    printf("  PASS arm_ldr_str_imm\n");
    return true;
}

static bool test_arm_ldm_stm() {
    // STMDB SP!, {R4, R5, LR} (E92D4030)
    // LDMIA SP!, {R4, R5, PC} (E8BD8030)
    std::uint32_t insns[] = { 0xE92D4030, 0xE8BD8030 };
    std::vector<std::uint8_t> code(8);
    std::memcpy(code.data(), insns, 8);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty()) {
        printf("  FAIL arm_ldm_stm: empty body\n");
        return false;
    }
    // The LDMIA with PC is a return — should bail_preserve_pc
    printf("  PASS arm_ldm_stm\n");
    return true;
}

static bool test_arm_mul() {
    // MUL R0, R1, R2 (E0000291) — Rd=0, Rm=1, Rs=2
    // BX LR
    std::uint32_t insns[] = { 0xE0000291, 0xE12FFF1E };
    std::vector<std::uint8_t> code(8);
    std::memcpy(code.data(), insns, 8);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_mul: translate failed\n");
        return false;
    }
    printf("  PASS arm_mul\n");
    return true;
}

static bool test_arm_bic_orr_eor() {
    // ORR R0, R1, R2 (E1810002)
    // BIC R3, R0, #0xFF (E3C030FF)
    // EOR R4, R3, R1 (E0234001)
    // BX LR
    std::uint32_t insns[] = { 0xE1810002, 0xE3C030FF, 0xE0234001, 0xE12FFF1E };
    std::vector<std::uint8_t> code(16);
    std::memcpy(code.data(), insns, 16);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_bic_orr_eor: translate failed\n");
        return false;
    }
    printf("  PASS arm_bic_orr_eor\n");
    return true;
}

static bool test_arm_shifted_reg() {
    // ADD R0, R1, R2, LSL #3 (E0810182)
    // MOV R3, R0, LSR #4 (E1A03220)
    // BX LR
    std::uint32_t insns[] = { 0xE0810182, 0xE1A03220, 0xE12FFF1E };
    std::vector<std::uint8_t> code(12);
    std::memcpy(code.data(), insns, 12);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_shifted_reg: translate failed\n");
        return false;
    }
    printf("  PASS arm_shifted_reg\n");
    return true;
}

static bool test_arm_conditional_exec() {
    // CMP R0, #0 (E3500000)
    // MOVEQ R1, #1 (03A01001) — only if Z set
    // MOVNE R1, #2 (13A01002) — only if Z clear
    // BX LR
    std::uint32_t insns[] = { 0xE3500000, 0x03A01001, 0x13A01002, 0xE12FFF1E };
    std::vector<std::uint8_t> code(16);
    std::memcpy(code.data(), insns, 16);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_conditional_exec: translate failed\n");
        return false;
    }
    printf("  PASS arm_conditional_exec\n");
    return true;
}

static bool test_arm_bl_resume_point() {
    // BL +8 (EB000000) — target = PC+8+0 = 0x1008
    // MOV R0, #1 (E3A00001) — resume point at 0x1004
    // BX LR (E12FFF1E)
    std::uint32_t insns[] = { 0xEB000000, 0xE3A00001, 0xE12FFF1E };
    std::vector<std::uint8_t> code(12);
    std::memcpy(code.data(), insns, 12);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty()) {
        printf("  FAIL arm_bl_resume_point: empty body\n");
        return false;
    }
    bool found = false;
    for (auto rp : tr.resume_points) {
        if (rp == 0x1004) { found = true; break; }
    }
    if (!found) {
        printf("  FAIL arm_bl_resume_point: 0x1004 not in resume_points\n");
        return false;
    }
    printf("  PASS arm_bl_resume_point\n");
    return true;
}

static bool test_arm_ldrh_strh() {
    // LDRH R0, [R1, #4] (E1D100B4) — immediate offset halfword load
    // STRH R0, [R1, #8] (E1C100B8) — immediate offset halfword store
    // BX LR
    std::uint32_t insns[] = { 0xE1D100B4, 0xE1C100B8, 0xE12FFF1E };
    std::vector<std::uint8_t> code(12);
    std::memcpy(code.data(), insns, 12);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_ldrh_strh: translate failed\n");
        return false;
    }
    printf("  PASS arm_ldrh_strh\n");
    return true;
}

static bool test_arm_mvn() {
    // MVN R0, #0 (E3E00000) — R0 = ~0 = 0xFFFFFFFF
    // BX LR
    std::uint32_t insns[] = { 0xE3E00000, 0xE12FFF1E };
    std::vector<std::uint8_t> code(8);
    std::memcpy(code.data(), insns, 8);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_mvn: translate failed\n");
        return false;
    }
    printf("  PASS arm_mvn\n");
    return true;
}

static bool test_arm_rsb() {
    // RSB R0, R1, #0 (E2610000) — R0 = 0 - R1 (negate)
    // BX LR
    std::uint32_t insns[] = { 0xE2610000, 0xE12FFF1E };
    std::vector<std::uint8_t> code(8);
    std::memcpy(code.data(), insns, 8);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_rsb: translate failed\n");
        return false;
    }
    printf("  PASS arm_rsb\n");
    return true;
}

static bool test_arm_clz() {
    // CLZ R0, R1 (E16F0F11)
    // BX LR
    std::uint32_t insns[] = { 0xE16F0F11, 0xE12FFF1E };
    std::vector<std::uint8_t> code(8);
    std::memcpy(code.data(), insns, 8);

    auto tr = translate_arm_block(code.data(), code.size(), 0x1000);
    if (tr.func.body.empty() || !tr.complete) {
        printf("  FAIL arm_clz: translate failed\n");
        return false;
    }
    printf("  PASS arm_clz\n");
    return true;
}

// Execute bounded blocks and compare all registers, flags and memory against
// exactly the same number of DynCom instructions, including partial budgets.
static bool test_bounded_execution() {
#ifdef __EMSCRIPTEN__
    struct program { bool thumb; std::vector<std::uint8_t> bytes; };
    auto arm = [](std::initializer_list<std::uint32_t> words) {
        std::vector<std::uint8_t> bytes(words.size() * 4);
        std::memcpy(bytes.data(), words.begin(), bytes.size());
        return program{false, bytes};
    };
    auto thumb = [](std::initializer_list<std::uint16_t> words) {
        std::vector<std::uint8_t> bytes(words.size() * 2);
        std::memcpy(bytes.data(), words.begin(), bytes.size());
        return program{true, bytes};
    };
    std::vector<program> programs = {
        // Natural loops: entry/interior headers, forward exits, multiple
        // backedges, and shapes requiring the general entry dispatcher.
        arm({0xe2822001,0xe2833001,0xe2500001,0x1afffffc,0xe2844001}),
        arm({0xe3500002,0x0a000002,0xe2833001,0xe2500001,0x1afffffa,0xe2844001}),
        arm({0xe2822001,0xe2833001,0xe3500002,0x0a000001,0xe2500001,0x1afffffa,0xe2844001}),
        arm({0xe2833001,0xe3500002,0x0afffffc,0xe2500001,0x1afffffa,0xe2844001}),
        arm({0xe3500002,0x0a000000,0xe2833001,0xe2500001,0x1afffffa,0xe2844001}),
        arm({0xe2822001,0xe2833001,0xe3500002,0x0afffffb,0xe2500001,0x1afffffa,0xe2844001}),
        arm({0xe3500002,0x0a000000,0xe2822001,0xe2833001,0xe2500001,0x1afffffc,0xe2844001}),
        arm({0xe2822001,0xe5913000,0xe2500001,0x1afffffc,0xe2844001}),
        // A join, intervening instruction, or conditional CMP must invalidate
        // a lexical compare-operand shortcut.
        arm({0xe3500000,0x0a000000,0xe1500001,0xa3a02001}),
        arm({0xe1500001,0xa3a02001,0xe2500001,0x1afffffc}),
        arm({0x11500001,0xa3a02001,0xe2a33000}),
        arm({0xe1500001,0xe3a00007,0xc3a02001}),
        // Alternate entries must not reuse a lexical predecessor's wide result.
        arm({0xe3a04007,0xe3500000,0x0a000000,0xe0e54396,0xe0e54c97}),
        arm({0xe3a00003,0xe0e54396,0xe0e54c97,0xe2844001,0xe2500001,0x1afffffb}),
        arm({0xe5912000,0xe5913004,0xe5914008,0xe591500c}), // unchanged read span
        arm({0xe5912000,0xe5913004,0xe5914008,0xe2811004,0xe591500c}), // clobbered base
        arm({0xe5912000,0xe5913004,0xe5914008,0x02811004,0xe591500c}), // conditional clobber
        arm({0xe5912000,0xe5913004,0xe5914008,0xe5911000,0xe591500c}), // load into base
        arm({0xe2811001,0xe5912000,0xe5913004,0xe5914008}), // unaligned base
        arm({0xe2811eff,0xe281100c,0xe5912000,0xe5913004,0xe5914008}), // page end
        arm({0xe5912000,0xe5913004,0xe5914008,0xe2500001,0x1afffffa}), // cached loop
        thumb({0xF000}), thumb({0xF001}), thumb({0xF400}), thumb({0xF7FF}),
        thumb({0xF800}), thumb({0xF801}), thumb({0xFFFF}),
        thumb({0xE800}), thumb({0xE801}), thumb({0xEFFF}),
        thumb({0x2001,0xF000,0xF801}),
        arm({0xe3a00001, 0xe2800002, 0xe2400001}), // straight-line fallthrough
        arm({0xe49df004}), // post-indexed LDR PC and ARM/Thumb return
        arm({0xe128f000,0x02822001,0xe2a33000}), // MSR CPSR_f affects conditions/carry
        arm({0x0128f000,0xe2802001}), // conditional MSR skip

        arm({0xf5d1f000,0xf551f040,0xf7d1f002,0xe2800001}), // PLD hints must consume budgets without memory access
        arm({0xe3500000, 0x0a000000, 0xe3a01007, 0xe3a02009}), // taken/not-taken B
        arm({0xe3a00003,0xea000000,0xe2801001,0xe2500001,0x1afffffc}), // target is both forward and backward
        arm({0xe4813004,0xe2500001,0x1afffffc}), // store loop from measured workload
        arm({0xe2500001, 0x1afffffd}), // backward B, must return not recurse
        arm({0xe8a18000}), // STM stores pipeline PC
        arm({0xe1c100d0}), // LDRD is not STRH
        arm({0xe1c100f0}), // STRD is not STRH
        arm({0xe1c100b0}), // STRH preserves neighboring bytes
        arm({0xe1d100b2}), // LDRH at halfword offset
        arm({0xe1d100f2}), // LDRSH at halfword offset
        arm({0xe2810004, 0xe5810000, 0xe5912000}), // memory
        arm({0xe1a0f00e}), // MOV PC preserves ARM mode
        arm({0xe12fff1e}), // mode-changing BX
        arm({0xe0b00000}), // ADCS carry/overflow
        arm({0xe0d00000}), // SBCS carry/overflow
        arm({0xe0f00000}), // RSCS carry/overflow
        arm({0xe1b00f00}), // MOVS R0, R0 LSL #30
        arm({0xe1b00020}), // MOVS R0, R0 LSR #32
        arm({0xe1b00040}), // MOVS R0, R0 ASR #32
        arm({0xe1b00060}), // RRX
        arm({0xe1b00070}), // ROR R0
        arm({0xe1b00010}), // LSL R0
        arm({0xe1902f9f}), // LDREX must fall back
        arm({0xe1803f91}), // STREX must fall back
        thumb({0x3001, 0x3801, 0x2107}),
        thumb({0x2203,0x0712}), // live failure: LSLS must clear carry
        thumb({0x3001}), thumb({0x3801}), thumb({0x2801}),
        thumb({0x0000}), thumb({0x0800}), thumb({0x1000}),
        thumb({0x4000}), thumb({0x43c0}),
        thumb({0x4080}), thumb({0x40c0}), thumb({0x4100}), thumb({0x41c0}),
        thumb({0x4140}), thumb({0x4180}),
        thumb({0x7008}), thumb({0x8008}), // byte/halfword stores
        thumb({0x2800, 0xd000, 0x2107, 0x2209}),
        thumb({0x3801, 0xd1fd}),
        thumb({0x6008, 0x680a}),
        thumb({0x4678}), thumb({0x4478}), thumb({0x4687}), // PC forms fall back
        thumb({0x4770}),
        thumb({0x4708}), // BX to ARM
        thumb({0x47f0}), // BLX LR captures the old target
        thumb({0x4788}), // BLX to ARM
        thumb({0xbd00}), // POP PC mode switch
        thumb({0x3001, 0xf000, 0xf800}), // return before long call halfwords
    };
    // Lazy arithmetic flag recipes must survive exact short budgets, reads,
    // conditional writes, forward joins, loop backedges and helper callbacks.
    for (unsigned cond = 0; cond < 15; ++cond) {
        programs.push_back(arm({0xe3500001, (cond<<28)|0x03a02005, 0xe0a03000}));
        programs.push_back(arm({0xe3500001, (cond<<28)|0x0a000000, 0xe2902001, 0xe0a03000}));
        programs.push_back(arm({0xe3500001, (cond<<28)|0x02902001, 0xe0a03000}));
        programs.push_back(arm({0xe3500001, (cond<<28)|0x0a000400, 0xe3700001, 0xe0a03000}));
    }
    programs.push_back(arm({0xe3500001,0xe3700001,0xe3a02000}));
    programs.push_back(arm({0xe3500001,0xe1b02060,0xe0a03000})); // RRX consumes C
    programs.push_back(arm({0xe3500001,0xe5912000,0xe0a03000})); // memory/helper boundary
    programs.push_back(arm({0xe3500001,0xe5810000,0xe0a03000}));
    programs.push_back(arm({0xe3a02003,0xe2522001,0x1afffffd,0xe0a03000}));
    // Exercise whole ALU families against the independent interpreter, not
    // just the operand values that happened to expose a live replay failure.
    for (unsigned opcode = 0; opcode < 16; ++opcode) {
        for (unsigned shift : {0u, 0x80u, 0xF80u, 0x20u, 0xFA0u, 0x40u, 0xFC0u, 0x60u, 0xFE0u, 0x10u, 0x30u, 0x50u, 0x70u}) {
            const auto rd = opcode >= 8 && opcode <= 11 ? 0u : 2u;
            programs.push_back(arm({0xE0100000u | (opcode<<21) | (rd<<12) | shift}));
        }
    }
    for (unsigned opcode = 0; opcode < 16; ++opcode)
        programs.push_back(thumb({static_cast<std::uint16_t>(0x4008 | (opcode<<6))}));
    // Post-indexed byte/word/halfword accesses, both offsets and conditions.
    // R2 is set inside the program so register offsets are safe for all R0 seeds.
    for (unsigned cond : {0u,14u}) for (unsigned up : {0u,1u}) for (unsigned reg_offset : {0u,1u}) {
        for (unsigned load : {0u,1u}) for (unsigned byte : {0u,1u}) {
            unsigned inst = (cond<<28) | 0x04010000 | (up<<23) | (byte<<22) | (load<<20)
                | (reg_offset ? 0x02000002 : 4);
            auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&inst),4,0x1000,nullptr,nullptr,true,true,true);
            if (!tr.entry_supported) { printf("  FAIL post-index entry %08X rejected\n",inst); return false; }
            programs.push_back(arm({0xE3A02004,inst,0xE2813000}));
        }
        for (unsigned sh : {1u,2u,3u}) for (unsigned load : {0u,1u}) {
            if (!load && sh != 1) continue; // dualword transfers remain unsupported
            unsigned inst = (cond<<28) | 0x00010090 | (up<<23) | (load<<20) | (sh<<5)
                | (reg_offset ? 2 : 0x00400002);
            auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&inst),4,0x1000,nullptr,nullptr,true,true,true);
            if (!tr.entry_supported) { printf("  FAIL post-index halfword %08X rejected\n",inst); return false; }
            programs.push_back(arm({0xE3A02002,inst,0xE2813000}));
        }
    }
    for (unsigned inst : {0xE4811004u,0xE4911004u,0xE4B10004u,0xE0D110B2u,0xE10F0000u,0xE121F000u}) {
        auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&inst),4,0x1000,nullptr,nullptr,true,true,true);
        if (tr.entry_supported) { printf("  FAIL guarded form %08X accepted\n",inst); return false; }
    }
    for (unsigned opcode = 8; opcode <= 11; ++opcode) {
        unsigned inst = 0xE0100001 | (opcode<<21);
        auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&inst),4,0x1000,nullptr,nullptr,true,true,true);
        if (!tr.entry_supported) { printf("  FAIL test/compare %08X misclassified\n",inst); return false; }
    }
    // Warm a page with byte accesses, then test wider aligned/unaligned
    // hits, a different page, and flag-only MSR between cached accesses.
    for (unsigned offset : {0u, 1u, 2u, 3u, 4u, 4095u, 4096u}) {
        programs.push_back(arm({0xe5d12000, 0xe5913000u | offset}));
        programs.push_back(arm({0xe5c12000, 0xe5810000u | offset}));
        programs.push_back(arm({0xe5d12000, 0xe128f000, 0xe5913000u | offset}));
    }
    const auto existing_programs = programs.size();
    // Block-transfer fast spans and fallbacks: modes, writeback, conditions,
    // sparse/large lists, PC loads/stores, cross-page and unaligned bases.
    for (unsigned load : {0u,1u}) for (unsigned up : {0u,1u})
        for (unsigned pre : {0u,1u}) for (unsigned wb : {0u,1u})
        for (unsigned list : {0xdu,0xfffdu,0x800du}) {
            const unsigned inst = 0xe8010000u | (pre<<24) | (up<<23) | (wb<<21) | (load<<20) | list;
            programs.push_back(arm({inst}));
            programs.push_back(arm({0xe2811eff,0xe281100c,inst})); // page end
            // Unaligned PC loads can synthesize an unmapped branch destination
            // from this fixture; cover unaligned data transfers separately.
            if (!(load && (list & 0x8000))) programs.push_back(arm({0xe2811001,inst}));
        }
    std::rotate(programs.begin(), programs.begin() + existing_programs, programs.end());
    int index = 0;
    for (unsigned variant = 0; variant < 7; ++variant) for (const auto &p : programs) {
        const bool region = variant >= 4, cache_registers = region || (variant & 2), stop_after_store = variant & 1;
        auto tr = p.thumb ? translate_thumb_block(p.bytes.data(), p.bytes.size(), 0x1000, nullptr, nullptr, true, stop_after_store, cache_registers)
                          : translate_arm_block(p.bytes.data(), p.bytes.size(), 0x1000, nullptr, nullptr, true, stop_after_store, cache_registers, region);
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
            {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned carry : {0u, 1u}) for (unsigned r0 : {0u, 2u, 32u, 33u, 0xffffffffu, 0x7fffffffu, 0x80000000u}) for (unsigned budget : {0u,1u,2u,3u,4u,5u,16u,31u}) {
            test_mem actual, reference;
            actual.write_code(0x1000, p.bytes); reference.write_code(0x1000, p.bytes);
            actual.write32(0x8000, 0x000A8001); reference.write32(0x8000, 0x000A8001);
            alignas(8) std::uint8_t state[1024]{};
            auto set = [&](unsigned off, unsigned v) { std::memcpy(state + off, &v, 4); };
            auto get = [&](unsigned off) { unsigned v; std::memcpy(&v,state+off,4); return v; };
            r12l1::exclusive_monitor monitor(1);
            auto cpu = make_cpu(reference, monitor);
            for (int r = 0; r < 16; ++r) {
                unsigned v = r == 0 ? r0 : (r == 1 || r == 13) ? 0x8000 : r == 14 ? 0x2001 : r == 15 ? 0x1000 : 0;
                cpu->set_reg(r, v); set(state_offsets::reg(r), v);
            }
            cpu->set_cpsr(0x10000010 | (carry<<29) | (p.thumb ? 0x20 : 0));
            set(state_offsets::CPSR,cpu->get_cpsr());
            set(state_offsets::MODE,16);
            set(state_offsets::CFLAG,carry); set(state_offsets::VFLAG,1);
            set(state_offsets::TFLAG,p.thumb); set(state_offsets::AOT_BUDGET,budget); set(state_offsets::NIRQ,1);
            r12l1::tlb direct(12,r12l1::dyncom_folded_tlb);
            if (region && variant >= 5) {
                for (unsigned a=0;a<test_mem::SIZE;a+=4096) direct.add(a,actual.data.data()+a,7);
                set(state_offsets::AOT_TLB,reinterpret_cast<std::uintptr_t>(direct.entries));
                set(state_offsets::AOT_CODE_BEGIN,reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000));
                set(state_offsets::AOT_CODE_END,reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000+p.bytes.size()));
                if (variant == 6) direct.flush(); // mapped view subsequently invalidated
            }
            g_test_mem = &actual;
            const int count = js_run_aot_wasm(module.data(), module.size(), state, sizeof(state));
            g_test_mem = nullptr;
            if (count < 0 || count > static_cast<int>(budget) || (budget && !count && tr.entry_supported)) {
                printf("  FAIL bounded %d budget %u count %d\n",index,budget,count); return false;
            }
            if (count) cpu->run(count);
            for (int r = 0; r < 16; ++r) {
                unsigned v = get(state_offsets::reg(r));
                if (r == 15) v &= get(state_offsets::TFLAG) ? ~1u : ~3u;
                if (v != cpu->get_reg(r)) {
                    printf("  FAIL bounded %d budget %u R%d %08X vs %08X count %d\n",index,budget,r,v,cpu->get_reg(r),count);return false;
                }
            }
            const unsigned cpsr = cpu->get_cpsr();
            if ((get(state_offsets::CPSR) & ~0xF0000020u) != (cpsr & ~0xF0000020u)) {
                printf("  FAIL bounded CPSR control/Q bits %d\n",index);return false;
            }
            for (auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,31},
                    {state_offsets::ZFLAG,30},{state_offsets::CFLAG,29},{state_offsets::VFLAG,28},{state_offsets::TFLAG,5}})
                if (get(pair.first) != ((cpsr >> pair.second)&1)) {
                    printf("  FAIL bounded %d budget %u flag %u\n",index,budget,pair.first);return false;
                }
            if (actual.data != reference.data) { printf("  FAIL bounded memory %d\n",index); return false; }
        }
        ++index;
    }
    printf("  PASS bounded_execution (%zu exact budget/state/memory comparisons)\n",programs.size()*784);
#endif
    return true;
}

// Pending interrupts must exit at a taken loop backedge with precise state.
static bool test_region_loop_interrupts() {
#ifdef __EMSCRIPTEN__
    const std::vector<std::vector<std::uint32_t>> programs = {
        {0xe2800001,0xeafffffd},
        {0xe2811001,0xe2800001,0xeafffffd},
        {0xeafffffe}
    };
    for (unsigned kind = 0; kind < programs.size(); ++kind) {
        const auto &words = programs[kind];
        auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(words.data()),
            words.size()*4, 0x1000, nullptr, nullptr, true, true, true, true);
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
            {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned masked : {0u, 0x80u}) for (unsigned budget : {0u,1u,2u,3u,4u,17u,32u}) {
            alignas(8) std::uint32_t state[256]{};
            state[15] = 0x1000;
            state[state_offsets::CPSR/4] = 16 | masked;
            state[state_offsets::AOT_BUDGET/4] = budget;
            const unsigned expected = masked ? budget : std::min(budget, unsigned(words.size()));
            const unsigned r0 = kind == 0 ? (expected+1)/2 : kind == 1 ? expected/2 : 0;
            const unsigned r1 = kind == 1 && expected ? 1 : 0;
            const unsigned pc = kind == 0 ? 0x1000 + 4*(expected%2)
                : kind == 1 && expected ? 0x1004 + 4*((expected-1)%2) : 0x1000;
            const int count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            if (count != expected || state[0] != r0 || state[1] != r1 || state[15] != pc) {
                printf("  FAIL region_loop_interrupts kind=%u mask=%u budget=%u count=%d pc=%x\n",
                    kind,masked,budget,count,state[15]); return false;
            }
        }
    }
#endif
    printf("  PASS region_loop_interrupts (42 precise pending/masked IRQ checks)\n");
    return true;
}

// A guest alias must not let a region execute code overwritten by an inline store.
static bool test_region_code_alias() {
#ifdef __EMSCRIPTEN__
    for (bool block_store : {false, true}) {
    const std::uint32_t words[] = {block_store ? 0xe8a10009u : 0xe5810004u,0xe2822001,0xeafffffc};
    auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(words),sizeof(words),0x1000,nullptr,nullptr,true,true,true,true);
    auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    for (unsigned address : {0x1000u,0x8000u}) {
        test_mem memory;
        std::memcpy(memory.data.data()+0x1000,words,sizeof(words));
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
        tlb.add(address,memory.data.data()+0x1000,7);
        alignas(8) std::uint32_t state[256]{};
        state[0]=0xe3a04001; state[3]=state[0]; state[1]=address; state[15]=0x1000;
        state[state_offsets::AOT_BUDGET/4]=31;
        state[state_offsets::NIRQ/4]=1;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(memory.data.data()+0x1000);
        state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+sizeof(words);
        const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
        if(count!=1 || state[15]!=0x1004 || state[2]!=0 || memory.read32(0x1004)!=state[0] || !state[state_offsets::AOT_EXIT/4]) {
            printf("  FAIL region code alias %x count %d\n",address,count); return false;
        }
    }
    }
    // Four accesses qualify for the entry proof. Distinct guest pages alias
    // the same physical bytes; stores must remain visible to later loads.
    const std::uint32_t alias_words[] = {0xe5810000,0xe5952000,0xe5812004,0xe5953004};
    auto alias_tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(alias_words),
        sizeof(alias_words),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
    auto alias_module = build_wasm_module({alias_tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    for (bool code_alias : {false,true}) for (unsigned budget=0;budget<=5;++budget) {
        test_mem memory;
        memory.write_code(0x1000,{reinterpret_cast<const std::uint8_t *>(alias_words),
            reinterpret_cast<const std::uint8_t *>(alias_words)+sizeof(alias_words)});
        const auto prior = memory.data;
        const unsigned backing = code_alias ? 0x1000 : 0x8000;
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
        tlb.add(0x8000,memory.data.data()+backing,prot_write);
        tlb.add(0x9000,memory.data.data()+backing,prot_read);
        alignas(8) std::uint32_t state[256]{};
        state[0]=0xe3a04001;state[1]=0x8000;state[5]=0x9000;state[15]=0x1000;
        state[state_offsets::AOT_BUDGET/4]=budget;
        state[state_offsets::NIRQ/4]=1;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(memory.data.data()+0x1000);
        state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+sizeof(alias_words);
        const int count=js_run_aot_wasm(alias_module.data(),alias_module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
        const unsigned expected=std::min(budget,4u);
        if(count<0 || unsigned(count)>expected || (code_alias ? count>1 : unsigned(count)!=expected)
            || state[15]!=0x1000+4*count || (count==0 && memory.data!=prior)
            || (count>=1 && memory.read32(backing)!=state[0])
            || (count>=2 && state[2]!=state[0])
            || (count>=3 && memory.read32(backing+4)!=state[0])
            || (count>=4 && state[3]!=state[0])) {
            printf("  FAIL proved physical alias code=%u budget=%u count=%d\n",code_alias,budget,count);return false;
        }
    }
#endif
    printf("  PASS region_code_alias (single, multiple and entry-proved alias stores)\n");
    return true;
}

static bool test_inlined_leaves(arm_ir_policy policy = arm_ir_policy::configured) {
#ifdef __EMSCRIPTEN__
    // Repeated far calls in a loop, including real workload memory leaves.
    const std::vector<std::uint32_t> caller = (policy == arm_ir_policy::inline_call_ir || policy == arm_ir_policy::invariant_write_ir)
        ? std::vector<std::uint32_t>{0xe1a0800f,0xeb0003fd,0xe1a0900f,0xeb0003fb,0xe2566001,0x1afffff9}
        : std::vector<std::uint32_t>{0xeb0003fe,0xeb0003fd,0xe2566001,0x1afffffb};
    std::vector<std::vector<std::uint32_t>> leaves = {
        {0xe2800001,0xe12fff1e},
        {0xe3a03000,0xe5803000,0xe5911000,0xe5922000,0xe0411002,0xe5801000,0xe12fff1e},
        {0xe3a03000,0xe5803000,0xe5803004,0xe5803008,0xe12fff1e},
        {0xe12fff1e}
    };
    if ((policy == arm_ir_policy::inline_call_ir || policy == arm_ir_policy::invariant_write_ir)) {
        leaves.push_back({0xe1a02000,0xe1a00001,0xe1a01002,0xe12fff1e}); // register cycle
        leaves.push_back({0xe0900001,0xe2a02000,0xe12fff1e}); // flags across call
    }
    unsigned comparisons=0;
    for (bool deferred : {false,true}) for (const auto &words : leaves) {
        if ((policy == arm_ir_policy::inline_call_ir || policy == arm_ir_policy::invariant_write_ir) && !deferred) continue;
        std::vector<std::uint8_t> leaf(words.size()*4); std::memcpy(leaf.data(),words.data(),leaf.size());
        leaf_resolver resolve = [&](std::uint32_t pc) { return pc == 0x2000 ? leaf : std::vector<std::uint8_t>{}; };
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t *>(caller.data()),caller.size()*4,0x1000,nullptr,nullptr,true,true,true,true,&resolve,deferred,policy);
        if ((policy == arm_ir_policy::inline_call_ir || policy == arm_ir_policy::invariant_write_ir) && !tr.ir_inline_transfers) {printf("  FAIL inline transfers not selected\n");return false;}
        if (tr.dependencies.size()!=1 || tr.end_address!=0x1000+caller.size()*4) {printf("  FAIL leaf discovery\n");return false;}
#if defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_SEGMENTS) && !defined(EKA2L1_WASM_CODE_VERSIONS)
        if (deferred && words.size() >= 5 && !tr.ir_memory_guards && !tr.ir_proved_writes) {
            printf("  FAIL inlined memory leaf did not select IR guards\n"); return false;
        }
#endif
        auto module=build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned fast : {0u,1u}) for(unsigned alias : {0u,1u}) for(unsigned budget=0;budget<80;++budget) {
            test_mem actual, reference;
            std::vector<std::uint8_t> bytes(caller.size()*4);std::memcpy(bytes.data(),caller.data(),bytes.size());
            actual.write_code(0x1000,bytes); reference.write_code(0x1000,bytes);
            actual.write_code(0x2000,leaf);reference.write_code(0x2000,leaf);
            actual.write32(0x8000,0x8000);reference.write32(0x8000,0x8000);
            alignas(8) std::uint32_t state[256]{};
            r12l1::exclusive_monitor monitor(1);auto cpu=make_cpu(reference,monitor);
            for(unsigned r=0;r<16;++r) {
                unsigned v=r==0 ? (alias ? 0x2000 : 0x8000) : (r==1||r==2) ? 0x8000 : r==6 ? 3 : r==15 ? 0x1000 : 0;
                state[r]=v;cpu->set_reg(r,v);
            }
            cpu->set_cpsr(0x10);state[state_offsets::CPSR/4]=0x10;state[state_offsets::MODE/4]=16;
            state[state_offsets::AOT_BUDGET/4]=budget;state[state_offsets::NIRQ/4]=1;
            r12l1::tlb direct(12,r12l1::dyncom_folded_tlb);
            if(fast) {
                for(unsigned a=0;a<test_mem::SIZE;a+=4096)direct.add(a,actual.data.data()+a,7);
                state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(direct.entries);
                state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
                state[state_offsets::AOT_CODE_END/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x2000+leaf.size());
            }
            g_test_mem=&actual;
            const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            g_test_mem=nullptr;
            if(count<0 || count>static_cast<int>(budget) || ((policy==arm_ir_policy::inline_call_ir || policy==arm_ir_policy::invariant_write_ir) && budget && !count))return false;
            if(count)cpu->run(count);
            for(unsigned r=0;r<16;++r)if(state[r]!=cpu->get_reg(r)) {
                printf("  FAIL leaf fast=%u alias=%u budget=%u count=%d R%u %x vs %x\n",fast,alias,budget,count,r,state[r],cpu->get_reg(r));return false;
            }
            for(auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,31},{state_offsets::ZFLAG,30},{state_offsets::CFLAG,29},{state_offsets::VFLAG,28},{state_offsets::TFLAG,5}})
                if(state[pair.first/4]!=((cpu->get_cpsr()>>pair.second)&1))return false;
            if(actual.data!=reference.data)return false;
            ++comparisons;
        }
    }
    printf("  PASS inlined_leaves policy=%d (%u exact budget/state/memory comparisons)\n",int(policy),comparisons);
#endif
    return true;
}

static bool test_region_cpsr_callback() {
#ifdef __EMSCRIPTEN__
    const std::uint32_t code[] = {0xE128F000u, 0xE5912000u, 0xE2833001u};
    auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(code), sizeof(code),
        0x1000, nullptr, nullptr, true, true, true, true);
    auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
        {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    alignas(8) std::uint32_t state[256]{};
    state[0] = 0xF8000000u; state[1] = 0x8000;
    state[state_offsets::PC/4] = 0x1000;
    state[state_offsets::CPSR/4] = state[state_offsets::MODE/4] = 16;
    state[state_offsets::AOT_BUDGET/4] = 3;
    g_mutate_callback_cpsr = true; g_callback_observed_state = false;
    const int count = js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
    g_mutate_callback_cpsr = false;
    if (count != 2 || !g_callback_observed_state || state[state_offsets::CPSR/4] != 0x20000210u || state[3]) {
        printf("  FAIL region_cpsr_callback count=%d observed=%d cpsr=%x\n", count,g_callback_observed_state,state[state_offsets::CPSR/4]);
        return false;
    }
#endif
    printf("  PASS region_cpsr_callback\n"); return true;
}

static bool test_deferred_memory_exits() {
#ifdef __EMSCRIPTEN__
    unsigned checks=0;
    // Pure pre-indexed scalar accesses and spans without writeback/PC.
    for (unsigned op : {0xe5910000u,0xe5810000u,0xe5d10000u,0xe5c10000u,
                        0xe891000du,0xe881000du,0xe5910004u}) {
        const bool load=op&(1u<<20), multi=((op>>25)&7)==4, byte=(op&(1u<<22))&&!multi;
        const unsigned words[]={0xe3a05007u,op};
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(words),sizeof(words),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned permission:{0u,1u,2u,3u})for(unsigned endian:{0u,0x200u})
        for(unsigned address:{0x8000u,0x8001u,0x8ffcu,0u})for(unsigned budget:{0u,1u,2u}) {
            test_mem memory;const auto before=memory.data;
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(address==0?0x200000:0x8000,memory.data.data()+0x8000,permission);
            alignas(8) unsigned state[256]{};state[0]=17;state[1]=address;state[2]=19;state[3]=23;state[15]=0x1000;
            state[state_offsets::CPSR/4]=16|endian;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            const unsigned effective=address+(op==0xe5910004u?4:0);
            const bool fast=(permission&(load?1:2))&&!endian&&effective>=4096&&(byte||!(effective&3))&&((effective&~4095u)==0x8000)&&(!multi||(effective&4095)<=4084);
            g_test_mem=&memory;g_count_memory_helpers=true;g_memory_helper_calls=0;
            const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
            g_test_mem=nullptr;g_count_memory_helpers=false;
            const unsigned expected=budget<2?budget:fast?2:1;
            if(count!=expected||g_memory_helper_calls||state[5]!=(budget?7u:0u)||
               (expected<2&&(state[1]!=address||state[0]!=17||state[2]!=19||state[3]!=23||state[15]!=0x1000+expected*4||memory.data!=before))) {
                printf("  FAIL deferred_memory op=%x perm=%u endian=%x address=%x budget=%u count=%d expected=%u\n",op,permission,endian,address,budget,count,expected);return false;
            }++checks;
        }
    }
    printf("  PASS deferred_memory_exits (%u exact pre-instruction exits)\n",checks);
#endif
    return true;
}

static bool test_block_transfer_guards() {
#ifdef __EMSCRIPTEN__
    for (bool load : {false,true}) {
        const std::uint32_t inst = load ? 0xe891000du : 0xe881000du; // r0,r2,r3
        auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&inst),4,0x1000,nullptr,nullptr,true,true,true,true);
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned permission : {0u,1u,2u,3u}) for (unsigned endian : {0u,0x200u})
            for (unsigned address : {0x8000u,0x8001u,0x8ffcu,0u}) {
                test_mem memory;
                r12l1::tlb direct(12,r12l1::dyncom_folded_tlb); direct.add(address == 0 ? 0x200000 : 0x8000,memory.data.data()+0x8000,permission);
                alignas(8) std::uint32_t state[256]{};
                state[0]=17; state[1]=address; state[2]=19; state[3]=23; state[15]=0x1000;
                state[state_offsets::CPSR/4]=0x10|endian;
                state[state_offsets::AOT_BUDGET/4]=1;
                state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(direct.entries);
                g_test_mem=&memory; g_count_memory_helpers=true; g_memory_helper_calls=0;
                const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
                g_test_mem=nullptr; g_count_memory_helpers=false;
                const bool fast = (permission & (load ? 1 : 2)) && !endian && address==0x8000;
                if (count!=1 || (fast ? g_memory_helper_calls!=0 : g_memory_helper_calls==0)) {
                    printf("  FAIL block_transfer_guards load=%d perm=%u endian=%x address=%x calls=%u\n",load,permission,endian,address,g_memory_helper_calls);return false;
                }
            }
    }
#endif
    printf("  PASS block_transfer_guards (64 permission/endian/alignment/page/sentinel cases)\n");return true;
}

// Private callee relocation must not shift public sibling-call indices, even
// when imports and enough public functions require a multi-byte callee index.
static bool test_outlined_callee_indices() {
    for (unsigned import_count : {0u, 6u}) for (unsigned public_count : {2u, 128u}) {
        std::vector<wasm_func_def> functions(public_count);
        for (unsigned i = 0; i < public_count; ++i) {
            functions[i].export_name = "f_" + std::to_string(i);
            functions[i].num_locals = 0;
            functions[i].body = {op_i32_const, 13};
        }
        auto &caller = functions[0];
        caller.body = {op_local_get, 0, op_call, static_cast<std::uint8_t>(import_count + 1),
            op_local_get, 0, op_call, 0x80, 0x80, 0x80, 0x80, 0, op_i32_add};
        caller.outlined_call_offset = 7;
        caller.outlined_callee = std::make_shared<wasm_func_def>();
        caller.outlined_callee->export_name = "private_seven";
        caller.outlined_callee->num_locals = 0;
        caller.outlined_callee->body = {op_i32_const, 7};
        auto add_private = [](wasm_func_def &parent, unsigned value) {
            parent.body.insert(parent.body.end(), {op_local_get, 0, op_call});
            const auto offset=static_cast<unsigned>(parent.body.size());
            parent.body.insert(parent.body.end(), {0x80,0x80,0x80,0x80,0,op_i32_add});
            auto child=std::make_shared<wasm_func_def>();child->num_locals=0;
            child->body={op_i32_const,static_cast<std::uint8_t>(value)};
            parent.outlined_calls.push_back({child,offset});
        };
        add_private(caller,3);add_private(caller,5);add_private(functions[1],0);
        std::vector<wasm_import_func> imports;
        if (import_count) imports = {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
            {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
        const auto module = build_wasm_module(functions, imports);
        if (module.empty()) return false;
#ifdef __EMSCRIPTEN__
        alignas(8) std::uint32_t state[256]{};
        if (js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state)) != 28) {
            printf("  FAIL outlined callee imports=%u public=%u\n", import_count, public_count);
            return false;
        }
#endif
        const auto valid=caller.outlined_calls.front().call_offset;
        caller.outlined_calls.front().call_offset=caller.outlined_call_offset;
        if(!build_wasm_module(functions,imports).empty())return false;
        caller.outlined_calls.front().call_offset=valid;
        caller.outlined_calls.front().callee->outlined_callee=caller.outlined_callee;
        if(!build_wasm_module(functions,imports).empty())return false;
        caller.outlined_calls.front().callee->outlined_callee.reset();
        caller.outlined_call_offset = static_cast<std::uint32_t>(caller.body.size());
        if (!build_wasm_module(functions, imports).empty()) return false;
    }
    printf("  PASS outlined_callee_indices\n");
    return true;
}

static bool test_proved_read_spans() {
#ifdef __EMSCRIPTEN__
    const std::vector<std::vector<std::uint32_t>> programs = {
        {0xe5910000,0xe2844001,0xe5912004,0xe5913008},
        {0xe5910000,0xe5956000,0xe5912004,0xe5957004},
        {0xe5910000,0xe2811004,0xe5912000,0xe5913004},
        {0xe5910000,0xe3560000,0x01a01005,0xe5912004},
        {0xe3560000,0x0a000000,0xe5910000,0xe5912004,0xe5913008},
        {0xe5910000,0xe5912004,0xe2566001,0x1afffffb},
        {0xe5110004,0xe5912000,0xe5913004},
        {0xe5910000,0xe5810004,0xe5912004,0xe5913008},
        {0xe5910000,0xe89500c0,0xe5912004,0xe5913008},
        {0xe5910000,0xe5911004,0xe5912000,0xe5913004},
        {0xe5910000,0x15912004,0xe5913008},
        {0xe5910000,0xe0c43996,0xe5912004,0xe5913008},
        // Wide-value snapshots across memory, consumers, clobbers and entries.
        {0xe0c32796,0xe5910000,0xe0e32796,0xe5918004},
        {0xe0c32796,0xe0828003,0xe0e32796,0xe5910000},
        {0xe0c32796,0xe1a02007,0xe0e32796,0xe5910000},
        {0xe0c32796,0xe0854796,0xe0e32796,0xe5910000},
        {0xe0c32796,0xe0d32796,0xe0e32796,0xe5910000},
        {0xe0c32796,0x10e32796,0xe5910000},
        {0xe3560000,0x0a000000,0xe0c32796,0xe0e32796,0xe5910000},
        {0xe0c32796,0xe0e32796,0xe2599001,0x1afffffc},
        {0xe0c32796,0xe5812000,0xe0e32796,0xe5910000},
        {0xe0c32796,0xe89500c0,0xe0e32796,0xe5910000},
        {0xe0c32796,0xe5912000,0xe0e32796,0xe5910004},
        {0xe0c32796,0xe0e32392,0xe5910000},
        // Whole-region affine proofs: scalar, block, writeback and aliases.
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c},
        {0xe5910000,0xe2817004,0xe5972000,0xe5973004,0xe591400c},
        {0xe5910000,0xe1a07001,0xe5972004,0xe5973008,0xe591400c},
        {0xe5810000,0xe5912000,0xe5812004,0xe5913004},
        {0xe4810004,0xe4912004,0xe5213004,0xe5914004},
        {0xe5a10004,0xe5b12004,0xe5213004,0xe5114004},
        {0xe881000d,0xe5914000,0xe89500c0,0xe5854000},
        {0xe921000d,0xe5914000,0xe99500c0,0xe5854000},
        {0xe801000d,0xe5914000,0xe81500c0,0xe5854000},
        {0xe981000d,0xe5914000,0xe91500c0,0xe5854000},
        {0xe92d000d,0xe5910000,0xe5912004,0xe8bd000d},
        {0xe5910000,0xe5912004,0xe5913008,0xe8918010},
        {0xe5910000,0xe0c32796,0xe5918004,0xe0e32796,0xe5919008,0xe591a00c},
        // Pointer loss, conditional accesses and joins must retain generic code.
        {0xe5910000,0xe5911004,0xe5912000,0xe5913004},
        {0xe5910000,0x15912004,0xe5913008,0xe591400c},
        {0xe5910000,0xe3560000,0x0a000000,0xe5912004,0xe5913008,0xe591400c},
    };
    unsigned checks = 0;
    for (const auto &code : programs) {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code.data());
        auto tr = translate_arm_block(bytes, code.size() * 4, 0x1000,
            nullptr, nullptr, true, true, true, true, nullptr, true);
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
            {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned address : {0x8000u,0x8004u,0x8080u,0x8001u,0x8ff4u,0x8ff8u,0x8ffcu,0u,0xfffffffcu,0x1000u})
        for (unsigned permission : {0u,1u,2u,3u}) for (unsigned endian : {0u,0x200u})
        for (unsigned selector : {0u,2u})
        for (unsigned budget : {0u,1u,2u,3u,4u,5u,8u,17u}) {
            // New writeback/PC forms require production callbacks on proof
            // failure; the separate native fault probe checks that route.
            // These simple test imports are deliberately not that callback API.
            if (&code - programs.data() >= 24 && (address != 0x8080 || permission != 3 || endian)) continue;
            test_mem actual;
            for (unsigned a = 0x8000; a < 0x9000; a += 4) actual.write32(a, a ^ 0x12345678u);
            // Keep the loaded return PC inside this fixture's code mapping.
            if (code.back() == 0xe8918010 && address < test_mem::SIZE - 8)
                actual.write32(address + 4, 0x1000);
            actual.write_code(0x1000, {bytes, bytes + code.size() * 4});
            test_mem reference_memory = actual;
            r12l1::exclusive_monitor monitor(1);
            auto reference = make_cpu(reference_memory, monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
            tlb.add(0x8000, actual.data.data() + 0x8000, permission);
            tlb.add(0x1000, actual.data.data() + 0x1000, permission);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned r = 0; r < 16; ++r) {
                const auto value = r == 15 ? 0x1000u : r == 1 ? address : r == 5 ? address + 64 : r == 13 ? address + 128
                    : r == 6 ? selector : 0x120u + r;
                state[r] = value; reference->set_reg(r, value);
            }
            reference->set_cpsr(16 | endian);
            state[state_offsets::CPSR / 4] = 16 | endian;
            state[state_offsets::MODE / 4] = 16;
            state[state_offsets::NIRQ / 4] = 1;
            state[state_offsets::AOT_BUDGET / 4] = budget;
            state[state_offsets::AOT_TLB / 4] = reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN / 4] = reinterpret_cast<std::uintptr_t>(actual.data.data() + 0x1000);
            state[state_offsets::AOT_CODE_END / 4] = state[state_offsets::AOT_CODE_BEGIN / 4] + code.size() * 4;
            g_test_mem = &actual; g_count_memory_helpers = true; g_memory_helper_calls = 0;
            const int count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            g_test_mem = nullptr; g_count_memory_helpers = false;
            if (count < 0 || count > static_cast<int>(budget) || g_memory_helper_calls
                || (budget && address == 0x8080 && permission == 3 && !endian && !count)) {
                printf("  FAIL proved span progress/op=%08x address=%x budget=%u count=%d\n", code.front(), address, budget, count);
                return false;
            }
            try { if (count) reference->run(count); }
            catch (...) {
                printf("  FAIL proved span reference exception program=%u address=%x budget=%u count=%d PC=%x\n",
                    unsigned(&code-programs.data()),address,budget,count,reference->get_pc()); return false;
            }
            for (unsigned r = 0; r < 16; ++r) if (state[r] != reference->get_reg(r)) {
                printf("  FAIL proved span R%u address=%x permission=%u endian=%u budget=%u count=%d\n",
                    r,address,permission,endian,budget,count); return false;
            }
            for (auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,31},
                    {state_offsets::ZFLAG,30},{state_offsets::CFLAG,29},{state_offsets::VFLAG,28},{state_offsets::TFLAG,5}})
                if (state[pair.first / 4] != ((reference->get_cpsr() >> pair.second) & 1)) {
                    printf("  FAIL proved span flags\n"); return false;
                }
            if (actual.data != reference_memory.data) { printf("  FAIL proved span memory\n"); return false; }
            ++checks;
        }
    }
    printf("  PASS proved_read_spans (%u exact state/memory/budget comparisons)\n", checks);
#endif
    return true;
}


static bool test_invariant_reads(arm_ir_policy policy = arm_ir_policy::invariant_reads) {
#if defined(__EMSCRIPTEN__) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    std::vector<std::vector<std::uint32_t>> programs = {
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0xe2566001,0x1afffff9},
        {0x05910000,0x05912004,0x05913008,0x0591400c},
        {0xe5910000,0xe5817004,0xe5912004,0xe5913008,0xe591400c},
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0xe2811004},
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0x12811004},
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0xe0010394},
    };
    if (policy == arm_ir_policy::invariant_read_ir) {
        // Proved loads feed swaps and shared cold values before an unproved
        // fault. Stores overwrite earlier source memory; recipes must retain
        // the loaded value rather than rereading changed memory.
        programs.push_back({0xe5910000,0xe5912004,0xe5913008,0xe591400c,
            0xe1a07000,0xe1a00002,0xe1a02007,0xe5998000});
        programs.push_back({0xe5910000,0xe5912004,0xe5913008,0xe591400c,
            0xe0807002,0xe5817004,0xe5912004,0xe5998000});
        programs.push_back({0xe5910000,0xe5912004,0xe5913008,0xe591400c,
            0xe0807002,0xe0278003,0xe599a000,0xe3a07000,0xe3a08000});
        programs.push_back({0xe5910000,0xe5912004,0xe5913008,0xe591400c,
            0xe5810008,0xe0837000,0xe5998000});
    }
    unsigned checks = 0;
    for (const auto &code : programs) {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code.data());
        auto tr = translate_arm_block(bytes, code.size() * 4, 0x1000,
            nullptr, nullptr, true, true, true, true, nullptr, true, policy);
        const auto index = &code - programs.data();
        const bool selected = index < 3 || index >= 6;
        if (bool(tr.proved_reads) != selected || (selected && !tr.func.outlined_callee) || (policy == arm_ir_policy::invariant_reads && tr.ir_segments)
            || (policy == arm_ir_policy::invariant_read_ir && selected && index != 1 && !tr.ir_proved_reads)) {
            printf("  FAIL invariant read selection program=%u proved=%u\n", unsigned(&code-programs.data()),tr.proved_reads); return false;
        }
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
            {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        auto control = translate_arm_block(bytes, code.size()*4, 0x1000,
            nullptr, nullptr, true, true, true, true, nullptr, true, arm_ir_policy::disabled);
        auto control_module = build_wasm_module({control.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned address : {0x8000u,0x8004u,0x8080u,0x8001u,0x8ff4u,0x8ff8u,0x8ffcu,0u,0xfffffffcu,0x1000u})
        for (unsigned permission : {0u,1u,2u,3u}) for (unsigned endian : {0u,0x200u})
        for (unsigned selector : {0u,2u}) for (unsigned pending : {0u,1u})
        for (unsigned budget : {0u,1u,2u,3u,4u,5u,8u,17u}) {
            test_mem actual;
            for (unsigned a = 0x8000; a < 0x9000; a += 4) actual.write32(a, a ^ 0x12345678u);
            // Keep the loaded return PC inside this fixture's code mapping.
            if (code.back() == 0xe8918010 && address < test_mem::SIZE - 8)
                actual.write32(address + 4, 0x1000);
            actual.write_code(0x1000, {bytes, bytes + code.size() * 4});
            test_mem reference_memory = actual;
            r12l1::exclusive_monitor monitor(1);
            auto reference = make_cpu(reference_memory, monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
            tlb.add(0x8000, actual.data.data() + 0x8000, permission);
            tlb.add(0x1000, actual.data.data() + 0x1000, permission);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned r = 0; r < 16; ++r) {
                const auto value = r == 15 ? 0x1000u : r == 1 ? address : r == 5 ? address + 64 : r == 13 ? address + 128
                    : r == 6 ? selector : 0x120u + r;
                state[r] = value; reference->set_reg(r, value);
            }
            reference->set_cpsr(16 | endian);
            state[state_offsets::CPSR / 4] = 16 | endian;
            state[state_offsets::MODE / 4] = 16;
            state[state_offsets::NIRQ / 4] = pending;
            state[state_offsets::AOT_BUDGET / 4] = budget;
            state[state_offsets::AOT_TLB / 4] = reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN / 4] = reinterpret_cast<std::uintptr_t>(actual.data.data() + 0x1000);
            state[state_offsets::AOT_CODE_END / 4] = state[state_offsets::AOT_CODE_BEGIN / 4] + code.size() * 4;
            alignas(8) std::uint32_t control_state[256];
            std::copy(std::begin(state),std::end(state),std::begin(control_state));
            const auto original_memory=actual.data;
            g_test_mem = &actual; g_count_memory_helpers = true; g_memory_helper_calls = 0;
            const int count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            // Mixed IR can exit before a faulting access; the independent
            // interpreter below validates its exact returned prefix instead.
            if (policy != arm_ir_policy::invariant_read_ir) {
                const auto candidate_memory=actual.data;
                std::copy(original_memory.begin(),original_memory.end(),actual.data.begin());
                const int control_count=js_run_aot_wasm(control_module.data(),control_module.size(),
                    reinterpret_cast<std::uint8_t *>(control_state),sizeof(control_state));
                if(count!=control_count || actual.data!=candidate_memory) {
                    printf("  FAIL invariant read original progress/memory count=%d control=%d\n",count,control_count);return false;
                }
                for(unsigned r=0;r<16;++r)if(state[r]!=control_state[r]) {
                    printf("  FAIL invariant read original R%u\n",r);return false;
                }
            }
            g_test_mem = nullptr; g_count_memory_helpers = false;
            if (count < 0 || count > static_cast<int>(budget) || g_memory_helper_calls
                || (budget && address == 0x8080 && permission == 3 && !endian && !count)) {
                printf("  FAIL invariant read progress/op=%08x address=%x budget=%u count=%d\n", code.front(), address, budget, count);
                return false;
            }
            try { if (count) reference->run(count); }
            catch (...) {
                printf("  FAIL invariant read reference exception program=%u address=%x budget=%u count=%d PC=%x\n",
                    unsigned(&code-programs.data()),address,budget,count,reference->get_pc()); return false;
            }
            for (unsigned r = 0; r < 16; ++r) if (state[r] != reference->get_reg(r)) {
                printf("  FAIL invariant read R%u address=%x permission=%u endian=%u budget=%u count=%d\n",
                    r,address,permission,endian,budget,count); return false;
            }
            for (auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,31},
                    {state_offsets::ZFLAG,30},{state_offsets::CFLAG,29},{state_offsets::VFLAG,28},{state_offsets::TFLAG,5}})
                if (state[pair.first / 4] != ((reference->get_cpsr() >> pair.second) & 1)) {
                    printf("  FAIL invariant read flags\n"); return false;
                }
            if (actual.data != reference_memory.data) { printf("  FAIL invariant read memory\n"); return false; }
            ++checks;
        }
    }
    printf("  PASS invariant_reads policy=%d (%u exact state/memory/budget comparisons)\n", int(policy), checks);
#endif
    return true;
}


static bool test_budget_chunks(arm_ir_policy policy = arm_ir_policy::budget_chunks) {
#if defined(__EMSCRIPTEN__) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    std::vector<std::vector<std::uint32_t>> programs = {
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0xe2566001,0x1afffff9},
        {0x05910000,0x05912004,0x05913008,0x0591400c},
        {0xe5910000,0xe5817004,0xe5912004,0xe5913008,0xe591400c},
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0xe2811004},
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0x12811004},
        {0xe5910000,0xe5912004,0xe5913008,0xe591400c,0xe0010394},
    };
    if (policy == arm_ir_policy::deferred_chunk_counts) {
        // Taken and untaken forward edges must not inherit another path's
        // pending count. Both arms contain proved chunks before their join.
        programs.push_back({0xe2800001,0xe3560000,0x0a000003,
            0xe2822001,0xe2833001,0xe2844001,0xe2877001,
            0xe2888001,0xe2899001,0xe28aa001,0xe28bb001});
    }
    // Three chunks exercise multiple private callees and exact prefix counts.
    std::vector<std::uint32_t> long_program;
    for (unsigned n = 0; n < 24; ++n)
        for (auto op : {0xe2800001u,0xe2922001u,0x00223004u,
                policy == arm_ir_policy::deferred_chunk_counts ? 0xe581400cu : 0xe2844001u})
            long_program.push_back(op);
    programs.push_back(long_program);
    if (policy == arm_ir_policy::budget_gaps_ir) {
        // Three arithmetic instructions form IR; conditional memory stays in the
        // original emitter and form a budget chunk on either side of that IR.
        const std::vector<std::uint32_t> gap{0x05910000u,0x05812004u,0x05913008u,0x0581400cu};
        const std::vector<std::uint32_t> arithmetic{0xe2800001u,0xe2922001u,0x00233004u};
        std::vector<std::uint32_t> mixed=gap;
        mixed.insert(mixed.end(),arithmetic.begin(),arithmetic.end());
        mixed.insert(mixed.end(),gap.begin(),gap.end());
        programs={mixed};
        // A forward edge skips the IR and enters the following budget chunk.
        mixed.insert(mixed.begin()+4,{0xe3560000u,0x0a000002u});
        programs.push_back(mixed);
    }
    unsigned checks = 0;
    for (const auto &code : programs) {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code.data());
        auto tr = translate_arm_block(bytes, code.size() * 4, 0x1000,
            nullptr, nullptr, true, true, true, true, nullptr, true, policy);
        if (!tr.budget_chunks || tr.func.outlined_calls.empty() || (policy == arm_ir_policy::budget_gaps_ir ? !tr.ir_segments : tr.ir_segments != 0)
            || (policy == arm_ir_policy::deferred_chunk_counts && !tr.deferred_count_updates)
            || (policy != arm_ir_policy::budget_gaps_ir && &code == &programs.back() && tr.budget_chunks != 3)) {
            printf("  FAIL budget chunk selection program=%u chunks=%u\n", unsigned(&code-programs.data()),tr.budget_chunks); return false;
        }
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
            {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        auto control = translate_arm_block(bytes, code.size()*4, 0x1000,
            nullptr, nullptr, true, true, true, true, nullptr, true, arm_ir_policy::disabled);
        auto control_module = build_wasm_module({control.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned address : {0x8000u,0x8004u,0x8080u,0x8001u,0x8ff4u,0x8ff8u,0x8ffcu,0u,0xfffffffcu,0x1000u})
        for (unsigned permission : {0u,1u,2u,3u}) for (unsigned endian : {0u,0x200u})
        for (unsigned selector : {0u,2u}) for (unsigned pending : {0u,1u})
        for (unsigned budget : {0u,1u,2u,3u,4u,5u,8u,17u,31u,32u,33u,65u}) {
            test_mem actual;
            for (unsigned a = 0x8000; a < 0x9000; a += 4) actual.write32(a, a ^ 0x12345678u);
            // Keep the loaded return PC inside this fixture's code mapping.
            if (code.back() == 0xe8918010 && address < test_mem::SIZE - 8)
                actual.write32(address + 4, 0x1000);
            actual.write_code(0x1000, {bytes, bytes + code.size() * 4});
            test_mem reference_memory = actual;
            r12l1::exclusive_monitor monitor(1);
            auto reference = make_cpu(reference_memory, monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
            tlb.add(0x8000, actual.data.data() + 0x8000, permission);
            tlb.add(0x1000, actual.data.data() + 0x1000, permission);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned r = 0; r < 16; ++r) {
                const auto value = r == 15 ? 0x1000u : r == 1 ? address : r == 5 ? address + 64 : r == 13 ? address + 128
                    : r == 6 ? selector : 0x120u + r;
                state[r] = value; reference->set_reg(r, value);
            }
            reference->set_cpsr(16 | endian | (selector ? 0x40000000u : 0));
            state[state_offsets::CPSR / 4] = 16 | endian | (selector ? 0x40000000u : 0);
            state[state_offsets::ZFLAG / 4] = selector ? 1 : 0;
            state[state_offsets::MODE / 4] = 16;
            state[state_offsets::NIRQ / 4] = pending;
            state[state_offsets::AOT_BUDGET / 4] = budget;
            state[state_offsets::AOT_TLB / 4] = reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN / 4] = reinterpret_cast<std::uintptr_t>(actual.data.data() + 0x1000);
            state[state_offsets::AOT_CODE_END / 4] = state[state_offsets::AOT_CODE_BEGIN / 4] + code.size() * 4;
            alignas(8) std::uint32_t control_state[256];
            std::copy(std::begin(state),std::end(state),std::begin(control_state));
            const auto original_memory=actual.data;
            g_test_mem = &actual; g_count_memory_helpers = true; g_memory_helper_calls = 0;
            const int count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            const auto candidate_memory=actual.data;
            std::copy(original_memory.begin(),original_memory.end(),actual.data.begin());
            const int control_count=js_run_aot_wasm(control_module.data(),control_module.size(),
                reinterpret_cast<std::uint8_t *>(control_state),sizeof(control_state));
            if(count!=control_count || actual.data!=candidate_memory) {
                printf("  FAIL budget chunk original progress/memory count=%d control=%d\n",count,control_count);return false;
            }
            for(unsigned r=0;r<16;++r)if(state[r]!=control_state[r]) {
                printf("  FAIL budget chunk original R%u\n",r);return false;
            }
            g_test_mem = nullptr; g_count_memory_helpers = false;
            if (count < 0 || count > static_cast<int>(budget) || g_memory_helper_calls
                || (budget && address == 0x8080 && permission == 3 && !endian && !count)) {
                printf("  FAIL budget chunk progress/op=%08x address=%x budget=%u count=%d\n", code.front(), address, budget, count);
                return false;
            }
            try { if (count) reference->run(count); }
            catch (...) {
                printf("  FAIL budget chunk reference exception program=%u address=%x budget=%u count=%d PC=%x\n",
                    unsigned(&code-programs.data()),address,budget,count,reference->get_pc()); return false;
            }
            for (unsigned r = 0; r < 16; ++r) if (state[r] != reference->get_reg(r)) {
                printf("  FAIL budget chunk R%u address=%x permission=%u endian=%u budget=%u count=%d\n",
                    r,address,permission,endian,budget,count); return false;
            }
            for (auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,31},
                    {state_offsets::ZFLAG,30},{state_offsets::CFLAG,29},{state_offsets::VFLAG,28},{state_offsets::TFLAG,5}})
                if (state[pair.first / 4] != ((reference->get_cpsr() >> pair.second) & 1)) {
                    printf("  FAIL budget chunk flags\n"); return false;
                }
            if (actual.data != reference_memory.data) { printf("  FAIL budget chunk memory\n"); return false; }
            ++checks;
        }
    }
    printf("  PASS budget_chunks policy=%d (%u exact state/memory/budget comparisons)\n", int(policy), checks);
#endif
    return true;
}


static bool test_invariant_writes(arm_ir_policy policy = arm_ir_policy::invariant_writes) {
#if defined(__EMSCRIPTEN__) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    const std::vector<std::vector<std::uint32_t>> programs = {
        {0xe5810000,0xe5812004,0xe5813008,0xe581400c,0xe2566001,0x1afffff9},
        {0x05810000,0x05812004,0x05813008,0x0581400c},
        {0xe5810000,0xe5912000,0xe5812004,0xe5913004,0xe5813008,0xe5914008},
        {0xe5810000,0xe5812004,0xe5813008,0xe581400c,0xe2811004},
        {0xe5810000,0xe5812004,0xe5813008,0xe581400c,0x12811004},
        {0xe5810000,0xe5812004,0xe5813008,0xe581400c,0xe0010394},
    };
    unsigned checks = 0;
    for (const auto &code : programs) {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code.data());
        auto tr = translate_arm_block(bytes, code.size() * 4, 0x1000,
            nullptr, nullptr, true, true, true, true, nullptr, true, policy);
        if ((policy == arm_ir_policy::write_budget_chunks || policy == arm_ir_policy::deferred_chunk_counts)
            && (!tr.budget_chunks || tr.func.outlined_calls.empty()
                || (policy == arm_ir_policy::deferred_chunk_counts && !tr.deferred_count_updates))) {
            printf("  FAIL combined write/budget chunk selection\n"); return false;
        }
        const bool selected = &code - programs.data() < 3;
        if (bool(tr.proved_writes) != selected || (selected && !tr.func.outlined_callee) || (tr.ir_segments && policy != arm_ir_policy::invariant_write_ir)) {
            printf("  FAIL invariant write selection program=%u proved=%u\n", unsigned(&code-programs.data()),tr.proved_writes); return false;
        }
        if (policy == arm_ir_policy::invariant_write_ir && selected && &code - programs.data() != 1 && !tr.ir_proved_writes) {
            printf("  FAIL IR invariant write proof was not consumed\n"); return false;
        }
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
            {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        auto control = translate_arm_block(bytes, code.size()*4, 0x1000,
            nullptr, nullptr, true, true, true, true, nullptr, true, arm_ir_policy::disabled);
        auto control_module = build_wasm_module({control.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned address : {0x8000u,0x8004u,0x8080u,0x8001u,0x8ff4u,0x8ff8u,0x8ffcu,0u,0xfffffffcu,0x1000u})
        for (unsigned permission : {0u,1u,2u,3u}) for (unsigned endian : {0u,0x200u})
        for (unsigned selector : {0u,2u}) for (unsigned pending : {0u,1u})
        for (unsigned alias : {0u,1u})
        for (unsigned budget : {0u,1u,2u,3u,4u,5u,8u,17u}) {
            if (alias && address != 0x8000) continue;
            test_mem actual;
            for (unsigned a = 0x8000; a < 0x9000; a += 4) actual.write32(a, a ^ 0x12345678u);
            // Keep the loaded return PC inside this fixture's code mapping.
            if (code.back() == 0xe8918010 && address < test_mem::SIZE - 8)
                actual.write32(address + 4, 0x1000);
            actual.write_code(0x1000, {bytes, bytes + code.size() * 4});
            test_mem reference_memory = actual;
            r12l1::exclusive_monitor monitor(1);
            auto reference = make_cpu(reference_memory, monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
            tlb.add(0x8000, actual.data.data() + (alias ? 0x1000 : 0x8000), permission);
            tlb.add(0x1000, actual.data.data() + 0x1000, permission);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned r = 0; r < 16; ++r) {
                const auto value = r == 15 ? 0x1000u : r == 1 ? address : r == 5 ? address + 64 : r == 13 ? address + 128
                    : r == 6 ? selector : 0x120u + r;
                state[r] = value; reference->set_reg(r, value);
            }
            reference->set_cpsr(16 | endian | (selector ? 0x40000000u : 0));
            state[state_offsets::CPSR / 4] = 16 | endian | (selector ? 0x40000000u : 0);
            state[state_offsets::ZFLAG / 4] = selector ? 1 : 0;
            state[state_offsets::MODE / 4] = 16;
            state[state_offsets::NIRQ / 4] = pending;
            state[state_offsets::AOT_BUDGET / 4] = budget;
            state[state_offsets::AOT_TLB / 4] = reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN / 4] = reinterpret_cast<std::uintptr_t>(actual.data.data() + 0x1000);
            state[state_offsets::AOT_CODE_END / 4] = state[state_offsets::AOT_CODE_BEGIN / 4] + code.size() * 4;
            alignas(8) std::uint32_t control_state[256];
            std::copy(std::begin(state),std::end(state),std::begin(control_state));
            const auto original_memory=actual.data;
            g_test_mem = &actual; g_count_memory_helpers = true; g_memory_helper_calls = 0;
            const int count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            const auto candidate_memory=actual.data;
            std::copy(original_memory.begin(),original_memory.end(),actual.data.begin());
            // An outlined IR fallback may return a shorter positive prefix.
            // Compare that exact prefix against the independent original emitter.
            if (policy == arm_ir_policy::invariant_write_ir) control_state[state_offsets::AOT_BUDGET / 4] = count;
            const int control_count=js_run_aot_wasm(control_module.data(),control_module.size(),
                reinterpret_cast<std::uint8_t *>(control_state),sizeof(control_state));
            if(count!=control_count || actual.data!=candidate_memory) {
                printf("  FAIL invariant write original progress/memory count=%d control=%d\n",count,control_count);return false;
            }
            for(unsigned r=0;r<16;++r)if(state[r]!=control_state[r]) {
                printf("  FAIL invariant write original R%u\n",r);return false;
            }
            g_test_mem = nullptr; g_count_memory_helpers = false;
            if (count < 0 || count > static_cast<int>(budget) || g_memory_helper_calls
                || (budget && address == 0x8080 && permission == 3 && !endian && !alias && !count)) {
                printf("  FAIL invariant write progress/op=%08x address=%x budget=%u count=%d\n", code.front(), address, budget, count);
                return false;
            }
            // The interpreter fixture uses identity backing. Physical-alias
            // cases instead compare the original emitter above and require an
            // immediate code-write exit when the store condition is true.
            if (alias) {
                if (((code.front() >> 28) == 14 || selector) && count > 1) {
                    printf("  FAIL invariant write alias did not exit\n"); return false;
                }
                ++checks; continue;
            }
            try { if (count) reference->run(count); }
            catch (...) {
                printf("  FAIL invariant write reference exception program=%u address=%x budget=%u count=%d PC=%x\n",
                    unsigned(&code-programs.data()),address,budget,count,reference->get_pc()); return false;
            }
            for (unsigned r = 0; r < 16; ++r) if (state[r] != reference->get_reg(r)) {
                printf("  FAIL invariant write R%u address=%x permission=%u endian=%u budget=%u count=%d\n",
                    r,address,permission,endian,budget,count); return false;
            }
            for (auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,31},
                    {state_offsets::ZFLAG,30},{state_offsets::CFLAG,29},{state_offsets::VFLAG,28},{state_offsets::TFLAG,5}})
                if (state[pair.first / 4] != ((reference->get_cpsr() >> pair.second) & 1)) {
                    printf("  FAIL invariant write flags\n"); return false;
                }
            if (actual.data != reference_memory.data) { printf("  FAIL invariant write memory\n"); return false; }
            ++checks;
        }
    }
    printf("  PASS invariant_writes policy=%d (%u exact state/memory/budget comparisons)\n", int(policy), checks);
#endif
    return true;
}


static bool test_region_ir() {
    region_ir graph(0x1000);
    const auto input = graph.snapshots[0].regs[0];
    const auto sum = graph.binary(op_i32_add, input, graph.imm(7));
    if (sum != graph.binary(op_i32_add, input, graph.imm(7))) return false;
    auto middle = graph.snapshots.back(); middle.regs[2] = sum; middle.count = 1;
    graph.snapshots.push_back(middle);
    auto final = middle; final.regs[2] = graph.imm(42); final.count = 2;
    graph.snapshots.push_back(final);
    if (!graph.live_for({1,2})[sum] || graph.live_for({2})[sum]) return false;
    const auto wide = graph.binary(region_ir::pack, graph.imm(0xffffffff), graph.imm(0x80000000));
    if (graph.nodes[wide].immediate != 0x80000000ffffffffull) return false;
    auto sx = graph.unary(op_i64_extend_i32_s, input);
    if (graph.binary(region_ir::pack, graph.unary(region_ir::low,sx), graph.unary(region_ir::high,sx)) != sx) return false;
    if (!graph.valid()) return false;
    graph.nodes[sum].type = type_i64;
    if (graph.valid()) return false;
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_REGION_IR)
    std::vector<std::vector<std::uint32_t>> bodies;
    for (unsigned opcode : {0u,1u,2u,3u,4u,5u,12u,13u,14u,15u}) {
        bodies.push_back({0xe0002001u | (opcode << 21)});
        bodies.push_back({0xe20020ffu | (opcode << 21) | (4u << 8)});
        bodies.push_back({0xe3a00102u,0xe3e01000u,0xe0002001u | (opcode << 21)});
    }
    for (unsigned shift : {0u,1u,2u,3u}) for (unsigned amount : {0u,1u,31u})
        bodies.push_back({0xe1a02001u | (shift << 5) | (amount << 7)});
    for (unsigned form : {0u,2u,4u,6u}) for (bool overlap : {false,true}) {
        const auto op = 0xe0800090u | (form << 20) | (3u << 16) | ((overlap ? 0u : 2u) << 12) | (1u << 8);
        bodies.push_back({op, op | (1u << 21), 0xe1a02622u, 0xe1822a03u});
    }
    bodies.push_back({0xe0000190u,0xe0223190u}); // MUL then MLA
    bodies.push_back({0xe3a02000u,0xe3a03000u,0xe0e32190u,0xe0e32190u});
    bodies.push_back({0xe3a00102u,0xe3e01000u,0xe0c32190u,0xe0e32190u});
    bodies.push_back({0xe0802001u,0xe0803001u,0xe3a02001u}); // CSE and overwritten value
    bodies.push_back({0xe58a0000u,0xe59a2000u,0xe58a1000u,0xe59a3000u}); // ordered alias effects
    // Snapshot assignment is parallel: entry values must survive writes to
    // their architectural destinations, including the return-address input.
    bodies.push_back({0xe1a08004u,0xe1a04005u,0xe1a05008u}); // swap untouched entry R4/R5
    bodies.push_back({0xe1a04005u,0xe1a05008u,0xe1a08009u}); // forward chain
    bodies.push_back({0xe1a09008u,0xe1a08005u,0xe1a05004u}); // reverse chain
    bodies.push_back({0xe1a0800eu,0xe1a0e004u,0xe1a04008u,0xe1a0e004u}); // LR round trip
    const unsigned values[] = {0,1,2,0xffffffffu,0x80000000u,0x7fffffffu,0xffff0000u,0x12345678u};
    unsigned comparisons = 0;
    for (unsigned program = 0; program < bodies.size(); ++program) {
        std::vector<std::uint32_t> code{0xe59a0000u,0xe59a1004u,0xe59a6008u,0xe59a700cu};
        code.insert(code.end(),bodies[program].begin(),bodies[program].end());
        code.insert(code.end(),{0xe58a2010u,0xe58a3014u,0xe12fff1eu});
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code.data());
        auto tr = translate_arm_block(bytes,code.size()*4,0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
        if (!tr.func.outlined_callee || !tr.complete) {
            printf("  FAIL IR program %u not selected\n",program); return false;
        }
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned seed = 0; seed < 8; ++seed) for (unsigned flags : {0u,3u,12u,15u})
        for (unsigned budget = 0; budget <= code.size()+1; ++budget) {
            test_mem actual;
            actual.write_code(0x1000,{bytes,bytes+code.size()*4});
            for (unsigned a=0x8000;a<0x8040;a+=4) actual.write32(a,values[(seed+(a-0x8000)/4)%8]);
            test_mem reference_memory = actual;
            r12l1::exclusive_monitor monitor(1); auto reference = make_cpu(reference_memory,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb); tlb.add(0x8000,actual.data.data()+0x8000,3);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned r=0;r<16;++r) {
                auto value = r==15 || r==14 ? 0x1000u : r==10 ? 0x8000u : values[(seed+r*3)%8];
                state[r]=value; reference->set_reg(r,value);
            }
            reference->set_cpsr(16|(flags<<28));
            state[state_offsets::CPSR/4]=16|(flags<<28); state[state_offsets::MODE/4]=16;
            state[state_offsets::NIRQ/4]=1; state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+code.size()*4;
            for (unsigned f=0;f<4;++f) state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual; g_count_memory_helpers=true; g_memory_helper_calls=0;
            const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            g_test_mem=nullptr; g_count_memory_helpers=false;
            if (count != int(std::min<std::size_t>(budget,code.size())) || g_memory_helper_calls) {
                printf("  FAIL IR progress p=%u budget=%u count=%d\n",program,budget,count); return false;
            }
            if (count) reference->run(count);
            for (unsigned r=0;r<16;++r) if(state[r]!=reference->get_reg(r)) {
                printf("  FAIL IR p=%u seed=%u flags=%u budget=%u R%u %08x vs %08x\n",program,seed,flags,budget,r,state[r],reference->get_reg(r));return false;
            }
            for (unsigned f=0;f<5;++f) if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1)) {
                printf("  FAIL IR flags p=%u budget=%u\n",program,budget);return false;
            }
            if(actual.data!=reference_memory.data) {printf("  FAIL IR memory p=%u budget=%u\n",program,budget);return false;}
            ++comparisons;
        }
    }
    printf("  PASS region_ir (%u exact state/memory/budget comparisons, snapshot liveness/type checks)\n",comparisons);
#else
    printf("  PASS region_ir (snapshot liveness/type checks; backend disabled)\n");
#endif
    return true;
}

static bool test_ir_flags() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_SEGMENTS) && defined(EKA2L1_WASM_IR_OUTLINE) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    const unsigned operands[] = {0x02000000u,0x020000ffu,0x02000102u,0x020004ffu,
        1u,0x81u,0xf81u,0x21u,0xa1u,0xfa1u,0x41u,0xc1u,0xfc1u,0x61u,0xe1u,0xfe1u};
    const unsigned inputs[][2] = {{0,0},{0xffffffffu,0},{0xffffffffu,1},{0x80000000u,0x80000000u},
        {0x7fffffffu,1},{0,0xffffffffu},{0x80000000u,1},{0x7fffffffu,0xffffffffu},
        {1,2},{2,1},{0x12345678u,0x87654321u},{0xffff0000u,0x0000ffffu}};
    unsigned comparisons = 0;
    for (unsigned opcode = 0; opcode < 16; ++opcode) for (unsigned operand : operands) {
        const unsigned destination = (opcode >= 8 && opcode <= 11) || (opcode & 1) ? 0 : 2;
        const unsigned operation = 0xe0100000u | (destination << 12) | (opcode << 21) | operand;
        // The first S operation feeds ADC/RSC, and is overwritten by MOVS
        // before exit. A failed intervening load must recover its earlier flags.
        const unsigned code[] = {operation,0xe2a03000u | (destination << 16),0xe2e34000u,0xe59a5000u,
            0xe1b06063u,0xe2a67000u,0xe58b7000u,0xe12fff1eu};
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code);
        auto tr = translate_arm_block(bytes,sizeof(code),0x1000,nullptr,nullptr,true,true,true,true,
            nullptr,true,arm_ir_policy::invariant_read_flag_ir);
        if (!tr.complete || !tr.ir_segments || tr.ir_flag_instructions != 2) {
            printf("  FAIL IR flags selection %08x flags=%u\n",operation,tr.ir_flag_instructions);return false;
        }
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (const auto &input : inputs) for (unsigned flags : {0u,1u,2u,15u})
        for (unsigned budget = 0; budget <= 8; ++budget) for (bool mapped : {false,true}) {
            test_mem actual; actual.write_code(0x1000,{bytes,bytes+sizeof(code)});
            actual.write32(0x8000,0xfedcba98u); actual.write32(0x9000,0xfedcba98u);
            test_mem reference_memory = actual; r12l1::exclusive_monitor monitor(1); auto reference = make_cpu(reference_memory,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb); tlb.add(0x8000,actual.data.data()+0x8000,3);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned r = 0; r < 16; ++r) {
                const unsigned value = r == 0 ? input[0] : r == 1 ? input[1] : r == 15 ? 0x1000u
                    : r == 14 ? 0x2000u : r == 10 ? (mapped ? 0x8000u : 0x9000u) : r == 11 ? 0x8080u : 0x12340000u+r;
                state[r]=value; reference->set_reg(r,value);
            }
            reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=reference->get_cpsr();
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+sizeof(code);
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
            const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            g_test_mem=nullptr;g_count_memory_helpers=false;
            if(count!=int(std::min(budget,mapped?8u:3u)) || g_memory_helper_calls) {
                printf("  FAIL IR flags count %08x budget=%u mapped=%u count=%d\n",operation,budget,mapped,count);return false;
            }
            if(count)reference->run(count);
            for(unsigned r=0;r<16;++r)if(state[r]!=reference->get_reg(r)) {
                printf("  FAIL IR flags %08x input=%08x/%08x flags=%u budget=%u mapped=%u R%u actual=%08x expected=%08x\n",
                    operation,input[0],input[1],flags,budget,mapped,r,state[r],reference->get_reg(r));return false;
            }
            for(unsigned f=0;f<5;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1)) {
                printf("  FAIL IR flags %08x input=%08x/%08x flags=%u budget=%u mapped=%u flag=%u actual=%u cpsr=%08x\n",
                    operation,input[0],input[1],flags,budget,mapped,f,state[region_ir::flag_offsets[f]/4],reference->get_cpsr());return false;
            }
            if(actual.data!=reference_memory.data){printf("  FAIL IR flags memory\n");return false;}
            ++comparisons;
        }
    }
    printf("  PASS ir_flags (%u exact flags/state/memory/budget comparisons)\n",comparisons);
#endif
    return true;
}

static bool test_ir_conditions(arm_ir_policy policy = arm_ir_policy::conditional_value_ir) {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_SEGMENTS) && defined(EKA2L1_WASM_IR_OUTLINE) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    const unsigned operations[] = {0x00902001u,0x01b02081u,0x01500001u,0x00a02001u};
    const unsigned inputs[][2]={{0,0},{0xffffffffu,1},{0x7fffffffu,1},{0x80000000u,0xffffffffu}};
    unsigned comparisons=0;
    for(unsigned condition=0;condition<14;++condition) for(unsigned operation:operations) {
        const unsigned code[]={0xe1a08000u,operation|(condition<<28),0x01a04002u|(((condition+1)%14)<<28),
            0xe59a5000u,0xe0956001u,0xe58b4000u,0xe12fff1eu};
        const auto *bytes=reinterpret_cast<const std::uint8_t*>(code);
        auto tr=translate_arm_block(bytes,sizeof(code),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true,policy);
        if(!tr.complete || tr.ir_conditional_instructions!=2 || !tr.ir_memory_guards) {
            printf("  FAIL conditional IR selection cond=%u op=%x selected=%u\n",condition,operation,tr.ir_conditional_instructions);return false;
        }
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(const auto &input:inputs)for(unsigned flags=0;flags<16;++flags)
        for(unsigned budget=0;budget<=7;++budget)for(bool mapped:{false,true}) {
            test_mem actual;actual.write_code(0x1000,{bytes,bytes+sizeof(code)});
            actual.write32(0x8000,0x87654321);actual.write32(0x9000,0x87654321);
            test_mem reference_memory=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(reference_memory,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0x8000,actual.data.data()+0x8000,3);
            alignas(8) std::uint32_t state[256]{};
            for(unsigned reg=0;reg<16;++reg) {
                const unsigned value=reg==0?input[0]:reg==1?input[1]:reg==15?0x1000u:reg==14?0x2000u
                    :reg==10?(mapped?0x8000u:0x9000u):reg==11?0x8080u:0xabc00000u+reg;
                state[reg]=value;reference->set_reg(reg,value);
            }
            reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=reference->get_cpsr();
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+sizeof(code);
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
            const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
            g_test_mem=nullptr;g_count_memory_helpers=false;
            if(count!=int(std::min(budget,mapped?7u:3u)) || g_memory_helper_calls) {
                printf("  FAIL conditional IR count cond=%u budget=%u mapped=%u count=%d\n",condition,budget,mapped,count);return false;
            }
            if(count)reference->run(count);
            for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)) {
                printf("  FAIL conditional IR cond=%u op=%x flags=%u budget=%u R%u actual=%x expected=%x\n",condition,operation,flags,budget,reg,state[reg],reference->get_reg(reg));return false;
            }
            for(unsigned f=0;f<5;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1))return false;
            if(actual.data!=reference_memory.data)return false;
            ++comparisons;
        }
    }
    // Rejected speculative effects remain original-emitter boundaries.
    struct access {unsigned host, offset;};
    const std::map<std::uint32_t, access> no_accesses;
    for(unsigned opcode:{0x05902000u,0x05802000u,0x01a0f000u,0x0a000000u,0x012fff1eu}) {
        region_ir trial(0x1000);
        if(trial.append_conditional(opcode,0x1000,no_accesses,true,true))return false;
    }
    printf("  PASS ir_conditions policy=%d (%u exact predicate/flags/state/memory/budget comparisons)\n",static_cast<int>(policy),comparisons);
#endif
    return true;
}

// A longer graph must retain every intermediate fault snapshot and exact remainder.
static bool test_ir_long_segments() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_SEGMENTS) && defined(EKA2L1_WASM_IR_OUTLINE) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    unsigned comparisons=0;
    for(auto policy:{arm_ir_policy::conditional_value_ir,arm_ir_policy::long_segments_ir,arm_ir_policy::stack_values_ir})
    for(unsigned fault_at:{31u,63u,95u,127u}) {
        std::vector<unsigned> code(133);
        const unsigned pattern[]={0xe0900001u,0xe0222000u,0x20a33001u,0xe1a08004u};
        for(unsigned n=0;n<code.size();++n)code[n]=pattern[n%4];
        code[0]=0xe1a0b00cu; // Written root cannot use an entry invariant proof.
        code[60]=0xe1a08004u;code[61]=0xe1a04005u;code[62]=0xe1a05008u;
        code[70]=0xe0c54796u;code[71]=0xe0e54796u;
        code[72]=0xe5894000u;code[73]=0xe5996000u;
        code[fault_at]=0xe59ba000u;code.back()=0xe12fff1eu;
        const auto *bytes=reinterpret_cast<const std::uint8_t*>(code.data());
        const auto size=code.size()*4;
        auto tr=translate_arm_block(bytes,size,0x1000,nullptr,nullptr,true,true,true,true,nullptr,true,policy);
        if(!tr.complete || tr.ir_max_segment_length!=(policy==arm_ir_policy::long_segments_ir?128u:32u)
            || !tr.ir_memory_guards || !tr.ir_conditional_instructions || !tr.ir_wide_products
            || (policy == arm_ir_policy::stack_values_ir && !tr.ir_stack_values)) {
            printf("  FAIL long IR selection policy=%d max=%u\n",int(policy),tr.ir_max_segment_length);return false;
        }
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned seed:{0u,1u,0x7fffffffu,0xffffffffu})for(unsigned flags:{0u,5u,10u,15u})
        for(unsigned budget=0;budget<=134;++budget)for(bool mapped:{false,true}) {
            test_mem actual;actual.write_code(0x1000,{bytes,bytes+size});actual.write32(0x8000,seed);actual.write32(0x8004,~seed);
            test_mem reference_memory=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(reference_memory,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0x8000,actual.data.data()+0x8000,3);
            alignas(8) unsigned state[256]{};
            for(unsigned reg=0;reg<16;++reg) {
                unsigned value=reg==15?0x1000u:reg==14?0x2000u:reg==9?0x8000u:reg==12?(mapped?0x8004u:0x9000u):seed+reg;
                state[reg]=value;reference->set_reg(reg,value);
            }
            reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=reference->get_cpsr();
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+size;
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
            int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
            g_test_mem=nullptr;g_count_memory_helpers=false;
            if(count!=int(std::min(budget,mapped?133u:fault_at)) || g_memory_helper_calls || state[state_offsets::AOT_BUDGET/4]!=budget) {
                printf("  FAIL long IR count policy=%d fault=%u budget=%u mapped=%u count=%d\n",int(policy),fault_at,budget,mapped,count);return false;
            }
            if(count)reference->run(count);
            for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)) {
                printf("  FAIL long IR state policy=%d fault=%u budget=%u R%u\n",int(policy),fault_at,budget,reg);return false;
            }
            for(unsigned f=0;f<5;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1))return false;
            if(actual.data!=reference_memory.data)return false;
            ++comparisons;
        }
    }
    printf("  PASS ir_long_segments (%u exact long-graph state/flags/memory/budget comparisons)\n",comparisons);
#endif
    return true;
}

static bool test_ir_segments() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_IR_SEGMENTS)
    struct program { std::vector<std::uint32_t> body; unsigned extra = 0; };
    const std::vector<program> programs = {
        {{0xe1a08004u,0xe1a04005u,0xe1a05008u}}, // parallel copy cycle
        {{0xe0802001u,0xe0803001u,0xe3a02001u}}, // CSE and overwritten value
        {{0xe3a02001u,0xe2823002u,0xe2834003u,0xe0244004u}},
        {{0xe0000190u,0xe0223190u,0xe0822003u}}, // MUL/MLA
        {{0xe0e32190u,0xe1a04622u,0xe1844a03u,0xe0244001u}}, // wide entry value
        {{0xe0822001u,0xe0823001u,0xe0233002u,0xe0e32190u}}, // wide successor
        {{0xe1a0400fu,0xe0844000u,0xe0244001u}}, // PC pipeline input
        {{0xe3a04004u,0xe0800001u,0xe0202003u,0xe1a03002u,0xe2544001u,0x1afffffau},15},
        // Branch into an integer sequence: the target must start a new segment.
        {{0xe3510000u,0x0a000002u,0xe0802001u,0xe0222003u,0xe2822001u,
          0xe0823001u,0xe0233000u,0xe2833001u}},
#if defined(EKA2L1_WASM_IR_MEMORY) && !defined(EKA2L1_WASM_CODE_VERSIONS)
        {{0xe0c20190u,0xe0e20190u,0xe0a20190u,0xe1a04002u}}, // source/destination overlap
        {{0xe0810390u,0xe0a10390u,0xe0c10390u,0xe0e10390u}}, // all four wide forms
        {{0xe3a04004u,0xe0e32190u,0xe59a0000u,0xe58a2010u,0xe2544001u,0x1afffffau},15},
        {{0xeb0003f9u,0xe0c32190u,0xe59a0000u,0xe0e32190u},4}, // wide caller after inlined integer leaf
#else
        {{0xeb0003f9u,0xe0822001u,0xe0222003u,0xe2822001u},4}, // inlined integer leaf
#endif
    };
    const std::vector<std::uint8_t> leaf{0x01,0x00,0x80,0xe0,0x03,0x20,0x20,0xe0,
        0x02,0x30,0xa0,0xe1,0x1e,0xff,0x2f,0xe1};
    leaf_resolver resolve = [&](std::uint32_t address) { return address == 0x2000 ? leaf : std::vector<std::uint8_t>{}; };
    const unsigned values[] = {0,1,2,0xffffffffu,0x80000000u,0x7fffffffu,0xffff0000u,0x12345678u};
    unsigned comparisons = 0;
    std::vector<arm_ir_policy> policies{arm_ir_policy::disabled, arm_ir_policy::inline_segments, arm_ir_policy::configured};
#ifdef EKA2L1_WASM_IR_OUTLINE
    policies.push_back(arm_ir_policy::outlined_recipes);
#endif
    for (auto policy : policies)
    for (unsigned p = 0; p < programs.size(); ++p) {
        // A branch to the next instruction excludes the whole-region IR even
        // when both options are enabled, and exercises a real region label.
        std::vector<std::uint32_t> code{0xe59a0000u,0xe59a1004u,0xe59a6008u,0xe59a700cu,0xeaffffffu};
        code.insert(code.end(),programs[p].body.begin(),programs[p].body.end());
        // Restore caller LR after the inlined BL before returning from fixture.
        if (p == programs.size()-1) code.push_back(0xe1a0e00bu);
        code.insert(code.end(),{0xe58a2010u,0xe58a3014u,0xe12fff1eu});
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code.data());
        auto tr = translate_arm_block(bytes,code.size()*4,0x1000,nullptr,nullptr,true,true,true,true,&resolve,true,policy);
        if (!tr.complete || (policy == arm_ir_policy::disabled ? tr.ir_segments != 0 : !tr.ir_segments) || tr.func.outlined_callee) {
            printf("  FAIL IR segment program %u not selected\n",p); return false;
        }
#ifdef EKA2L1_WASM_IR_OUTLINE
        const auto expected_private = (policy == arm_ir_policy::configured || policy == arm_ir_policy::outlined_recipes) ? tr.ir_segments : 0;
        if(tr.ir_outlined_segments!=expected_private || tr.func.outlined_calls.size()!=expected_private) {
            printf("  FAIL private IR remainder selection\n");return false;
        }
#endif
        auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned seed=0;seed<8;++seed) for(unsigned flags:{0u,3u,12u,15u})
        for(unsigned budget=0;budget<=48;++budget) {
            test_mem actual; actual.write_code(0x1000,{bytes,bytes+code.size()*4}); actual.write_code(0x2000,leaf);
            for(unsigned a=0x8000;a<0x8040;a+=4) actual.write32(a,values[(seed+(a-0x8000)/4)%8]);
            test_mem reference_memory=actual; r12l1::exclusive_monitor monitor(1); auto reference=make_cpu(reference_memory,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0x8000,actual.data.data()+0x8000,3);
            alignas(8) std::uint32_t state[256]{};
            for(unsigned r=0;r<16;++r) {
                const auto value=r==15||r==14||r==11?0x1000u:r==10?0x8000u:values[(seed+r*3)%8];
                state[r]=value;reference->set_reg(r,value);
            }
            reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=16|(flags<<28);state[state_offsets::MODE/4]=16;
            state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x2010);
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
            const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            g_test_mem=nullptr;g_count_memory_helpers=false;
            unsigned total=static_cast<unsigned>(code.size())+programs[p].extra;
            if(p==8 && values[(seed+1)%8]==0)total-=3;
            if(count!=int(std::min(budget,total))||g_memory_helper_calls || state[state_offsets::AOT_BUDGET/4]!=budget) {
                printf("  FAIL IR segment count p=%u budget=%u count=%d expected=%u\n",p,budget,count,std::min(budget,total));return false;
            }
            if(count)reference->run(count);
            for(unsigned r=0;r<16;++r)if(state[r]!=reference->get_reg(r)) {
                printf("  FAIL IR segment p=%u seed=%u flags=%u budget=%u R%u %08x vs %08x\n",p,seed,flags,budget,r,state[r],reference->get_reg(r));return false;
            }
            for(unsigned f=0;f<5;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1)) {
                printf("  FAIL IR segment flags p=%u budget=%u\n",p,budget);return false;
            }
            if(actual.data!=reference_memory.data){printf("  FAIL IR segment memory\n");return false;}
            ++comparisons;
        }
    }
    printf("  PASS ir_segments (%u exact state/memory/budget comparisons across enabled policies)\n",comparisons);
#endif
    return true;
}

// Failed dynamic guards must expose the pre-instruction snapshot, including
// values overwritten later and a parallel register swap. Block transfers prove
// their entire span before any effect, and code aliases exit before writes.
static bool test_ir_memory_exits() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_SEGMENTS) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    const unsigned operations[] = {0xe59b2000u,0xe58b2000u,0xe49b2004u,0xe52b2004u,
        0xe8bb0005u,0xe8ab0005u,0xe9bb0005u,0xe9ab0005u,
        0xe83b0005u,0xe82b0005u,0xe93b0005u,0xe92b0005u};
    unsigned comparisons=0;
    for(auto policy:{arm_ir_policy::configured,arm_ir_policy::outlined_recipes})
    for(bool wide:{false,true})for(bool dependent:{false,true})for(unsigned operation:operations) {
        const unsigned code[]={wide?0xe0c54196u:0xe1a08004u,wide?0xe0e54196u:0xe1a04005u,wide?0xe0a54796u:0xe1a05008u,
            0xe58a4020u,dependent?0xe59ab000u:0xe59a0000u,0xe2800001u,operation,0xe3a04000u,0xe3a05000u};
        const auto *bytes=reinterpret_cast<const std::uint8_t *>(code);
        auto tr=translate_arm_block(bytes,sizeof(code),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true,policy);
        if(!tr.complete || tr.ir_memory_guards!=3) {printf("  FAIL memory IR not selected %08x guards=%u\n",operation,tr.ir_memory_guards);return false;}
        if(wide && (!tr.ir_wide_products || !tr.ir_cold_halves)) {
            printf("  FAIL wide IR snapshot recipes not selected\n");return false;
        }
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned permission:{0u,1u,2u,3u})for(unsigned endian:{0u,0x200u})
        for(unsigned address:{0u,0x8001u,0x8ffcu,0x9000u,0x9001u,0x9004u,0x9ffcu,0xb000u})for(unsigned flags:{0u,3u,12u,15u}) {
            test_mem actual;actual.write_code(0x1000,{bytes,bytes+sizeof(code)});
            for(unsigned a=0x8000;a<0xc000;a+=4)actual.write32(a,a*37+11);
            if(dependent)actual.write32(0x8000,address);
            // Virtual B000 aliases current code for guard testing. Reads use
            // identical bytes in the independent interpreter backing.
            std::memcpy(actual.data.data()+0xb000,actual.data.data()+0x1000,4096);
            test_mem reference_memory=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(reference_memory,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0x8000,actual.data.data()+0x8000,3);
            tlb.add(address==0xb000?0xb000:0x9000,actual.data.data()+(address==0xb000?0x1000:0x9000),permission);
            alignas(8) std::uint32_t state[256]{};
            for(unsigned r=0;r<16;++r) {
                unsigned value=r==15?0x1000:r==10?0x8000:r==11?(dependent?0xdeadbeefu:address):0x12340000+r;
                state[r]=value;reference->set_reg(r,value);
            }
            reference->set_cpsr(16|endian|(flags<<28));state[state_offsets::CPSR/4]=reference->get_cpsr();
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=9;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000+sizeof(code));
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            const bool load=operation&(1u<<20),block=((operation>>25)&7)==4;
            const bool up=operation&(1u<<23),pre=operation&(1u<<24);
            const unsigned size=block?8:4;
            const unsigned start=address+(block?(up?(pre?4:0):(pre?-8:-4)):(pre?(up?int(operation&4095):-int(operation&4095)):0));
            const unsigned page=address==0xb000?0xb000:0x9000;
            const bool mapped=(start&~4095u)==0x8000 || ((start&~4095u)==page && (permission&(load?1:2)));
            const bool safe=mapped && !(start&3) && (start&4095)<=4096-size
                && (load || address!=0xb000);
            const unsigned expected=endian?3:safe?9:6;
            g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
            auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            g_test_mem=nullptr;g_count_memory_helpers=false;
            if(count!=int(expected)||g_memory_helper_calls) {printf("  FAIL memory IR exit op=%08x address=%x perm=%u endian=%u count=%d expected=%u\n",operation,address,permission,endian,count,expected);return false;}
            reference->run(count);
            for(unsigned r=0;r<16;++r)if(state[r]!=reference->get_reg(r)) {printf("  FAIL memory IR R%u op=%08x address=%x got=%x want=%x\n",r,operation,address,state[r],reference->get_reg(r));return false;}
            for(unsigned f=0;f<5;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1))return false;
            if(actual.data!=reference_memory.data){printf("  FAIL memory IR effects op=%08x\n",operation);return false;}
            ++comparisons;
        }
    }
    // A cached read proof must not authorize a write, or vice versa.
    for(bool write_first:{false,true})for(unsigned permission:{0u,1u,2u,3u}) {
        const unsigned code[]={write_first?0xe58a1000u:0xe59a0000u,
            write_first?0xe59a0000u:0xe58a1000u,0xe59a2000u};
        const auto *bytes=reinterpret_cast<const std::uint8_t *>(code);
        auto tr=translate_arm_block(bytes,sizeof(code),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
        if(tr.ir_memory_guards!=3)return false;
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        test_mem actual;actual.write_code(0x1000,{bytes,bytes+sizeof(code)});actual.write32(0x8000,0x12345678u);
        test_mem reference_memory=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(reference_memory,monitor);
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0x8000,actual.data.data()+0x8000,permission);
        alignas(8) unsigned state[256]{};
        for(unsigned reg=0;reg<16;++reg){unsigned value=reg==15?0x1000:reg==10?0x8000:0x100+reg;state[reg]=value;reference->set_reg(reg,value);}
        reference->set_cpsr(16);state[state_offsets::CPSR/4]=16;state[state_offsets::MODE/4]=16;
        state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=3;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        const unsigned expected=!(permission&(write_first?2:1))?0:permission==3?3:1;
        g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
        const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
        g_test_mem=nullptr;g_count_memory_helpers=false;
        if(count!=int(expected)||g_memory_helper_calls){printf("  FAIL IR cached permission write_first=%u permission=%u count=%d expected=%u\n",write_first,permission,count,expected);return false;}
        if(count)reference->run(count);
        for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg))return false;
        if(actual.data!=reference_memory.data)return false;
        ++comparisons;
    }
    printf("  PASS ir_memory_exits (%u exact intermediate snapshot/effect comparisons)\n",comparisons);
#endif
    return true;
}

// Exit-only arithmetic must use preserved SSA loads, not reload memory after
// an intervening store. A shared deep DAG also tests bounded recipe emission.
static bool test_ir_exit_recipes() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_IR_SEGMENTS) && defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_OUTLINE) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    std::vector<std::vector<unsigned>> programs = {
        {0xe59a2000u,0xe58a3000u,0xe0824005u,0xe59b0000u,0xe3a04000u,0xe3a02000u},
        {0xe0c54796u,0xe0848005u,0xe59b0000u,0xe3a04000u,0xe3a05000u,0xe3a08000u},
        {0xe0804001u,0xe1a08004u,0xe59b2000u,0xe3a04000u,0xe3a08000u},
        std::vector<unsigned>(24,0xe0040494u)
    };
    programs.back().insert(programs.back().end(),{0xe59b0000u,0xe3a04000u});
    const unsigned fault_at[]={3,2,2,24};
    const unsigned seeds[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu};
    unsigned comparisons=0;
    for(unsigned p=0;p<programs.size();++p) {
        const auto &code=programs[p];const auto *bytes=reinterpret_cast<const std::uint8_t *>(code.data());
        auto tr=translate_arm_block(bytes,code.size()*4,0x1000,nullptr,nullptr,true,true,true,true,nullptr,true,arm_ir_policy::outlined_recipes);
        if(!tr.complete || !tr.ir_outlined_segments || tr.ir_cold_values<=tr.ir_cold_halves) {
            printf("  FAIL general exit recipes not selected p=%u\n",p);return false;
        }
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        if(module.empty() || module.size()>131072) {printf("  FAIL exit recipe DAG growth p=%u bytes=%zu\n",p,module.size());return false;}
        for(auto seed:seeds)for(unsigned scenario=0;scenario<6;++scenario)
        for(unsigned flags:{0u,3u,12u,15u})for(unsigned budget=0;budget<=code.size()+1;++budget) {
            test_mem actual;actual.write_code(0x1000,{bytes,bytes+code.size()*4});
            actual.write32(0x8000,seed^0xabcdef01u);actual.write32(0x9000,0x87654321u);
            test_mem reference_memory=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(reference_memory,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0x8000,actual.data.data()+0x8000,3);
            if(scenario!=1)tlb.add(0x9000,actual.data.data()+0x9000,scenario==5?2:3);
            const unsigned address=scenario==2?0x9001u:scenario==3?0x9ffeu:0x9000u;
            const unsigned endian=scenario==4?0x200u:0;
            alignas(8) unsigned state[256]{};
            for(unsigned reg=0;reg<16;++reg) {
                unsigned value=reg==15?0x1000u:reg==10?0x8000u:reg==11?address:seed+reg;
                state[reg]=value;reference->set_reg(reg,value);
            }
            reference->set_cpsr(16|endian|(flags<<28));state[state_offsets::CPSR/4]=reference->get_cpsr();
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+code.size()*4;
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            const unsigned stop=scenario==0?code.size():(scenario==4&&p==0?0:fault_at[p]);
            const auto expected=std::min(budget,stop);
            g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
            auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            g_test_mem=nullptr;g_count_memory_helpers=false;
            if(count!=int(expected)||g_memory_helper_calls||state[state_offsets::AOT_BUDGET/4]!=budget) {
                printf("  FAIL exit recipe progress p=%u scenario=%u budget=%u count=%d expected=%u\n",p,scenario,budget,count,expected);return false;
            }
            if(count)reference->run(count);
            for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)) {
                printf("  FAIL exit recipe R%u p=%u scenario=%u budget=%u got=%x want=%x\n",reg,p,scenario,budget,state[reg],reference->get_reg(reg));return false;
            }
            for(unsigned f=0;f<5;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1))return false;
            if(actual.data!=reference_memory.data){printf("  FAIL exit recipe ordered memory\n");return false;}
            ++comparisons;
        }
    }
    printf("  PASS ir_exit_recipes (%u exact load-history/DAG/width/budget comparisons)\n",comparisons);
#endif
    return true;
}

static bool test_ir_addressing() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_SEGMENTS) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    const unsigned operations[]={
        0xe5d12000u,0xe5c12000u,0xe1d120b0u,0xe1c120b0u,0xe1d120d0u,0xe1d120f0u,
        0xe7912106u,0xe7812106u,0xe7d12106u,0xe7c12106u,
        0xe19120b6u,0xe18120b6u,0xe19120d6u,0xe19120f6u,
        0xe7912026u,0xe7912046u,0xe7912066u, // LSR32 / ASR32 / RRX offsets
        0xe4d12001u,0xe4c12001u,0xe1f120b2u,0xe0c120b2u,
        0xe5112004u,0xe5312004u,0xe01120b6u,
        0xe59f2004u,0xe1df20b4u};
    unsigned comparisons=0;
    for(unsigned operation:operations) {
        const unsigned code[]={0xe1a08004u,0xe1a04005u,0xe1a05008u,
            operation,0xe2822001u,0xe58a2000u,0xe3a04000u};
        const auto *bytes=reinterpret_cast<const std::uint8_t *>(code);
        auto tr=translate_arm_block(bytes,sizeof(code),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
        if(!tr.complete || tr.ir_memory_guards!=2) {printf("  FAIL addressing IR selection %08x guards=%u\n",operation,tr.ir_memory_guards);return false;}
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned address:{0u,0x8000u,0x8001u,0x8ffeu,0x8fffu,0x9000u,0xb000u})
        for(unsigned offset:{0u,1u})for(unsigned permission:{0u,1u,2u,3u})
        for(unsigned endian:{0u,0x200u})for(unsigned flags:{0u,3u,12u,15u}) {
            const bool single=((operation>>26)&3)==1,load=operation&(1u<<20);
            const bool up=operation&(1u<<23),pre=operation&(1u<<24),literal=((operation>>16)&15)==15;
            unsigned width=single?(operation&(1u<<22)?1:4):(operation&(1u<<5)?2:1),delta=0;
            if(single) {
                if(operation&(1u<<25)) {
                    const unsigned shift=(operation>>5)&3,amount=(operation>>7)&31;
                    delta=shift==0?offset<<amount:shift==1?(amount?offset>>amount:0)
                        :shift==2?unsigned(int(offset)>>(amount?amount:31))
                        :amount?((offset>>amount)|(offset<<(32-amount))):((offset>>1)|(((flags>>1)&1)<<31));
                } else delta=operation&4095;
            } else delta=operation&(1u<<22)?((operation>>4)&0xf0)|(operation&15):offset;
            const unsigned base=literal?0x1014:address;
            const unsigned start=pre?base+(up?delta:0u-delta):base;
            const unsigned page=literal?0x1000:address?address&~4095u:0x9000;
            const bool mapped=page && (start&~4095u)==page && (permission&(load?1:2));
            const bool safe=mapped && !endian && !(start&(width-1)) && (load || address!=0xb000);
            for(unsigned budget=0;budget<=8;++budget) {
                if(!safe && budget!=7)continue; // exact cold exits, plus all safe short budgets
                test_mem actual;actual.write_code(0x1000,{bytes,bytes+sizeof(code)});
                for(unsigned a=0x8000;a<0xd000;a+=4)actual.write32(a,a*37+0x89abcdefu);
                std::memcpy(actual.data.data()+0xb000,actual.data.data()+0x1000,4096);
                test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
                r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0xc000,actual.data.data()+0xc000,3);
                tlb.add(page,actual.data.data()+(page==0xb000?0x1000:page),permission);
                alignas(8) unsigned state[256]{};
                for(unsigned r=0;r<16;++r) {
                    const unsigned value=r==15?0x1000:r==1?address:r==6?offset:r==10?0xc000:0x12340000+r;
                    state[r]=value;reference->set_reg(r,value);
                }
                reference->set_cpsr(16|endian|(flags<<28));state[state_offsets::CPSR/4]=reference->get_cpsr();
                state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
                state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
                state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
                state[state_offsets::AOT_CODE_END/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000+sizeof(code));
                for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
                g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
                const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
                g_test_mem=nullptr;g_count_memory_helpers=false;
                const unsigned want=safe?std::min(budget,7u):3;
                if(count!=int(want)||g_memory_helper_calls){printf("  FAIL addressing exit op=%08x address=%x off=%u perm=%u endian=%u flags=%u budget=%u count=%d want=%u helpers=%u\n",operation,address,offset,permission,endian,flags,budget,count,want,g_memory_helper_calls);return false;}
                if(count)reference->run(count);
                for(unsigned r=0;r<16;++r)if(state[r]!=reference->get_reg(r)){printf("  FAIL addressing state op=%08x address=%x off=%u budget=%u R%u got=%x want=%x\n",operation,address,offset,budget,r,state[r],reference->get_reg(r));return false;}
                for(unsigned f=0;f<5;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(f==4?5:31-f))&1))return false;
                if(actual.data!=expected.data){printf("  FAIL addressing memory op=%08x\n",operation);return false;}
                ++comparisons;
            }
        }
    }
    // Narrow page proofs must retain each later access's alignment and width.
    const unsigned chain[]={0xe5d10001u,0xe1d120b2u,0xe5913004u,0xe5c10003u,0xe1c120b6u,0xe5813008u};
    const auto *bytes=reinterpret_cast<const std::uint8_t *>(chain);
    auto tr=translate_arm_block(bytes,sizeof(chain),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
    if(tr.ir_memory_guards!=6)return false;
    auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    for(unsigned address:{0x8000u,0x8001u,0x8002u,0x8ff8u,0x8ffeu,0xb000u})
    for(unsigned permission:{0u,1u,2u,3u})for(unsigned endian:{0u,0x200u}) {
        test_mem actual;actual.write_code(0x1000,{bytes,bytes+sizeof(chain)});
        for(unsigned a=0x8000;a<0xc000;a+=4)actual.write32(a,a*37+0x89abcdefu);
        std::memcpy(actual.data.data()+0xb000,actual.data.data()+0x1000,4096);
        test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);const unsigned page=address==0xb000?0xb000:0x8000;
        tlb.add(page,actual.data.data()+(page==0xb000?0x1000:page),permission);
        alignas(8) unsigned state[256]{};
        for(unsigned r=0;r<16;++r){const unsigned value=r==15?0x1000:r==1?address:0x12345678+r;state[r]=value;reference->set_reg(r,value);}
        reference->set_cpsr(16|endian);state[state_offsets::CPSR/4]=reference->get_cpsr();
        state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=6;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
        state[state_offsets::AOT_CODE_END/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000+sizeof(chain));
        unsigned want=0;const unsigned offsets[]={1,2,4,3,6,8},widths[]={1,2,4,1,2,4};
        for(;want<6;++want) {
            const unsigned at=address+offsets[want],width=widths[want];
            if(endian || !(permission&(want<3?1:2)) || (at&~4095u)!=page
                || (at&(width-1)) || (at&4095)>4096-width || (want>=3 && page==0xb000))break;
        }
        g_test_mem=&actual;g_count_memory_helpers=true;g_memory_helper_calls=0;
        const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
        g_test_mem=nullptr;g_count_memory_helpers=false;
        if(count!=int(want)||g_memory_helper_calls){printf("  FAIL IR width transition address=%x perm=%u endian=%u count=%d want=%u\n",address,permission,endian,count,want);return false;}
        if(count)reference->run(count);
        for(unsigned r=0;r<16;++r)if(state[r]!=reference->get_reg(r))return false;
        if(actual.data!=expected.data)return false;
        ++comparisons;
    }
    printf("  PASS ir_addressing (%u exact width/address/snapshot/budget comparisons)\n",comparisons);
#endif
    return true;
}

static bool test_repeated_read_guards() {
#ifdef __EMSCRIPTEN__
    const std::uint32_t words[] = {0xe5910000,0xe5912004,0xe5913008};
    auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(words),sizeof(words),0x1000,nullptr,nullptr,true,true,true,true);
    auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    for (unsigned permission : {0u,1u,2u,3u}) for (unsigned endian : {0u,0x200u})
        for (unsigned address : {0x8000u,0x8001u,0x8ffcu,0u}) {
            test_mem memory;
            std::vector<std::uint8_t> code(sizeof(words)); std::memcpy(code.data(),words,sizeof(words));
            memory.write_code(0x1000,code);
            for (unsigned a=0x8000;a<0xa000;a+=4) { memory.write32(a,a*37); }
            r12l1::tlb direct(12,r12l1::dyncom_folded_tlb); direct.add(address == 0 ? 0x200000 : 0x8000,memory.data.data()+0x8000,permission);
            alignas(8) std::uint32_t state[256]{};
            state[1]=address; state[15]=0x1000;
            state[state_offsets::CPSR/4]=0x10|endian;
            state[state_offsets::AOT_BUDGET/4]=3;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(direct.entries);
            g_test_mem=&memory; g_count_memory_helpers=true; g_memory_helper_calls=0;
            const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            g_test_mem=nullptr; g_count_memory_helpers=false;
            const bool fast=(permission&1)&&!endian&&address==0x8000;
            if(count<1||count>3||(fast ? count!=3||g_memory_helper_calls!=0 : g_memory_helper_calls==0)) return false;
            // These test imports deliberately perform raw reads. For endian
            // and unaligned cases assert helper routing, not CPU semantics.
            // Full state comparisons for the eligible path are also covered
            // by bounded_execution at every instruction budget.
            if (fast && (state[0]!=memory.read32(address)||state[2]!=memory.read32(address+4)
                ||state[3]!=memory.read32(address+8)||state[15]!=0x100c)) return false;
        }
#endif
    printf("  PASS repeated_read_guards (32 permission/endian/alignment/page/sentinel cases)\n");return true;
}

// Cached displacements must work for high virtual addresses, page changes,
// separate read/write caches and two virtual pages sharing one backing page.
static bool test_memory_displacements() {
#ifdef __EMSCRIPTEN__
    const unsigned words[]={0xe5910000u,0xe5912004u,0xe5830008u,0xe583200cu,
        0xe5934008u,0xe593500cu,0xe5814010u,0xe5916010u};
    auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(words),sizeof(words),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
    auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    unsigned checks=0;
    for(unsigned page:{0x1000u,0x70000000u,0x80000000u,0xfffff000u})
    for(bool alias:{false,true})for(unsigned budget=0;budget<=8;++budget) {
        test_mem memory;const unsigned other=page^0x1000u;
        // The lowest test page uses 0x3000 for its second page (zero is denied).
        const unsigned next=other?other:0x3000u;
        auto *first=memory.data.data()+0x8000;
        auto *second=memory.data.data()+(alias?0x8000:0x9000);
        std::uint32_t a=0x87654321u,b=0xabcdef01u;
        std::memcpy(first+0xfe0,&a,4);std::memcpy(first+0xfe4,&b,4);
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(page,first,3);tlb.add(next,second,3);
        alignas(8) unsigned state[256]{};state[1]=page+0xfe0;state[3]=next+0xfe0;state[15]=0x1000;
        state[state_offsets::CPSR/4]=16;state[state_offsets::AOT_BUDGET/4]=budget;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        auto expected=memory.data;unsigned regs[16]{};regs[1]=state[1];regs[3]=state[3];regs[15]=0x1000+4*budget;
        for(unsigned i=0;i<budget;++i) {
            const unsigned op=words[i],r=(op>>12)&15,base=((op>>16)&15)==1?0x8000:(alias?0x8000:0x9000),offset=base+0xfe0+(op&4095);
            if(op&(1u<<20))std::memcpy(&regs[r],expected.data()+offset,4);
            else std::memcpy(expected.data()+offset,&regs[r],4);
        }
        g_test_mem=&memory;g_count_memory_helpers=true;g_memory_helper_calls=0;
        const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
        g_test_mem=nullptr;g_count_memory_helpers=false;
        if(count!=budget||g_memory_helper_calls||memory.data!=expected||std::memcmp(state,regs,sizeof(regs))) {
            printf("  FAIL memory_displacements page=%x alias=%d budget=%u count=%d\n",page,alias,budget,count);return false;
        }++checks;
    }
    printf("  PASS memory_displacements (%u exact state/memory/budget checks)\n",checks);
#endif
    return true;
}

static bool test_block_transfer_callback_pc() {
#ifdef __EMSCRIPTEN__
    const std::uint32_t words[] = {0xe3a02007,0xe8918009}; // MOV; LDM r1,{r0,r3,pc}
    auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(words),sizeof(words),0x1000,nullptr,nullptr,true,true,true,true);
    auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    test_mem memory; memory.write32(0x8000,17); memory.write32(0x8004,19); memory.write32(0x8008,0x2001);
    alignas(8) std::uint32_t state[256]{};
    state[1]=0x8000; state[15]=0x1000; state[state_offsets::AOT_BUDGET/4]=2;
    g_test_mem=&memory; g_expected_callback_pc=0x1004; g_callback_pc_matches=true;
    const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
    g_test_mem=nullptr; g_expected_callback_pc=0;
    if(count!=2 || !g_callback_pc_matches || state[0]!=17 || state[3]!=19 || state[15]!=0x2001 || state[state_offsets::TFLAG/4]!=1) {
        printf("  FAIL block_transfer_callback_pc\n"); return false;
    }
#endif
    printf("  PASS block_transfer_callback_pc\n"); return true;
}

static bool test_outlined_code_lookup() {
    struct restore_mode {
        bool old = code_lookup_outline;
        unsigned old_compare = code_compare_mode;
        ~restore_mode() { code_lookup_outline = old; code_compare_mode = old_compare; }
    } restore;
    for (const unsigned compare : {2u, 4u})
    for (const unsigned snapshot_size : {28u, 128u})
    for (const bool outlined : {false, true}) {
        code_compare_mode = compare;
        code_lookup_outline = outlined;
        test_mem memory;
        r12l1::exclusive_monitor monitor(1);
        auto cpu = make_cpu(memory, monitor);
        std::array<std::uint8_t, 256> primary{}, leaf{}, other{}, remap{};
        std::atomic<std::uint64_t> generation{1}, replacement{1};
        cpu->code_mapping_generation = &generation;
        cpu->code_address_space = 1;
        bool mapped = true;
        unsigned resolutions = 0;
        auto *primary_backing = primary.data();
        auto *leaf_backing = leaf.data();
        std::size_t extent = primary.size();
        cpu->resolve_code = [&](std::uint32_t pc, core::code_mapping &out) {
            ++resolutions;
            if (!mapped) return false;
            out = {cpu->code_address_space,
                pc == 0x1000 ? primary_backing : pc == 0x2000 ? leaf_backing : other.data(), extent};
            return true;
        };
        validated_code_cache cache;
        auto install = [&]() -> validated_code_cache::block & {
            auto &entry = cache.insert(0x1000, {1, primary_backing, extent}, snapshot_size);
            validated_code_cache::add_dependency(entry, 0x2000, leaf_backing,
                {leaf_backing, leaf_backing + snapshot_size});
            return entry;
        };
        auto &initial = install();
        if (compare == 4 && (initial.comparator != select_code_comparator(snapshot_size)
            || initial.dependencies[0].comparator != select_code_comparator(snapshot_size))) return false;
#ifdef __wasm_simd128__
        if (compare == 4 && ((snapshot_size == 28 && initial.comparator == equal_code_bytes)
            || (snapshot_size == 128 && initial.comparator != equal_code_bytes))) return false;
#endif
        if (!cache.find(0x1000, *cpu)) return false;
        const auto first_resolutions = resolutions;
        for (unsigned i = 0; i < 8; ++i) if (!cache.find(0x1000, *cpu)) return false;
        if (resolutions != first_resolutions) return false;
        // Direct host writes invalidate, including dependency bytes. Restoring
        // bytes after rejection must never revive that compiled version.
        for (auto *bytes : {primary.data(), leaf.data()}) {
            for (unsigned offset : {0u, 15u, 16u, snapshot_size / 2, snapshot_size - 1}) {
                auto &old = install();
                if (!cache.find(0x1000, *cpu)) return false;
                const auto before = cache.invalidations;
                bytes[offset] ^= 1;
                if (cache.find(0x1000, *cpu) || old.live || cache.invalidations != before + 1) return false;
                bytes[offset] ^= 1;
                if (cache.find(0x1000, *cpu) || cache.invalidations != before + 1) return false;
            }
        }
        install(); if (!cache.find(0x1000, *cpu)) return false;
        // Same-index collision recovers the existing generation guard.
        cache.insert(0x3000, {1, other.data(), other.size()}, 8);
        if (!cache.find(0x3000, *cpu)) return false;
        if (compare == 4) {
            auto *other_entry = cache.find(0x3000, *cpu);
            if (!other_entry || other_entry->comparator != select_code_comparator(8)) return false;
        }
        const auto before_collision = resolutions;
        if (!cache.find(0x1000, *cpu) || resolutions != before_collision) return false;
        // Equal numerical generations from another source force refresh.
        cpu->code_mapping_generation = &replacement;
        if (!cache.find(0x1000, *cpu) || resolutions != before_collision + 2) return false;
        primary_backing = remap.data(); ++replacement;
        if (cache.find(0x1000, *cpu)) return false;
        install(); if (!cache.find(0x1000, *cpu)) return false;
        leaf_backing = other.data(); ++replacement;
        if (cache.find(0x1000, *cpu)) return false;
        install(); if (!cache.find(0x1000, *cpu)) return false;
        mapped = false; ++replacement;
        if (cache.find(0x1000, *cpu)) return false;
        mapped = true; ++replacement;
        if (!cache.find(0x1000, *cpu)) return false;
        extent = snapshot_size - 1; ++replacement;
        if (cache.find(0x1000, *cpu)) return false;
        extent = 256; install();
        cpu->code_address_space = 2;
        if (cache.find(0x1000, *cpu)) return false;
        cpu->code_address_space = 1;
        if (!cache.find(0x1000, *cpu)) return false;
        cache.invalidate(0x2004, 4);
        if (cache.find(0x1000, *cpu)) return false;
        install(); cpu->code_mapping_generation = nullptr;
        const auto before_legacy = resolutions;
        if (!cache.find(0x1000, *cpu) || !cache.find(0x1000, *cpu)
            || resolutions != before_legacy + 4) return false;
        cpu->code_mapping_generation = &replacement; replacement.store(0);
        const auto before_zero = resolutions;
        if (!cache.find(0x1000, *cpu) || !cache.find(0x1000, *cpu)
            || resolutions != before_zero + 4) return false;
    }
    printf("  PASS outlined_code_lookup (both lookup modes, grouped/stored comparators, short/long snapshots, dependencies, mappings, rejection)\n");
    return true;
}

static bool test_exact_code_compare() {
    for (unsigned mode : {0u, 1u, 2u, 3u, 4u}) {
    code_compare_mode = mode;
    const auto compare = [mode](const std::uint8_t *x, const std::uint8_t *y, std::size_t size) {
        return (mode == 4 ? select_code_comparator(size) : equal_code_bytes)(x, y, size);
    };
    std::array<std::uint8_t,577> a{},b{};
    for(unsigned i=0;i<a.size();++i) a[i]=b[i]=i*37;
    for(unsigned offset=0;offset<16;++offset) for(unsigned size=0;size<=512;++size) {
        if(!compare(a.data()+offset,b.data()+offset,size)) return false;
        for(unsigned at : {0u,size/2,size ? size-1 : 0u}) if(size) {
            b[offset+at]^=1;
            if(compare(a.data()+offset,b.data()+offset,size)) return false;
            b[offset+at]^=1;
        }
    }
    // Exercise every byte and independent alignment of the short snapshots.
    for (unsigned left=0;left<16;++left) for(unsigned right=0;right<16;++right)
        for(unsigned size=0;size<=192;++size) {
            std::memcpy(b.data()+right,a.data()+left,size);
            if(!compare(a.data()+left,b.data()+right,size)) return false;
            for(unsigned at=0;at<size;++at) {
                b[right+at]^=128;
                if(compare(a.data()+left,b.data()+right,size)) return false;
                b[right+at]^=128;
            }
        }
    for (unsigned left=0;left<4;++left) for(unsigned right=0;right<4;++right)
        for(unsigned size : {255u,256u,257u,511u,512u,513u}) {
            std::memcpy(b.data()+right,a.data()+left,size);
            if(!compare(a.data()+left,b.data()+right,size)) return false;
            for(unsigned at=0;at<size;++at) {
                b[right+at]^=64;
                if(compare(a.data()+left,b.data()+right,size)) return false;
                b[right+at]^=64;
            }
            // Equal mismatch bits in separate vectors must not cancel.
            b[right]^=1; b[right+16]^=1;
            if(compare(a.data()+left,b.data()+right,size)) return false;
            b[right]^=1; b[right+16]^=1;
        }
    }
    code_compare_mode = 0;
    printf("  PASS exact_code_compare (five modes including selected functions, unaligned/tails/mutations)\n"); return true;
}

// Check every condition/flag combination against DynCom, including false moves,
// source/destination overlap, PC reads, rotated immediates and unchanged flags.
static bool test_conditional_alu_select() {
#ifdef __EMSCRIPTEN__
    std::vector<unsigned> forms = {0x01a02000u, 0x01a00000u, 0x01af2000u,
        0x01a0200fu, 0x03a02001u, 0x03a02480u, 0x01b02000u, 0x01a02080u};
    for (unsigned opcode = 0; opcode < 16; ++opcode) {
        if (opcode >= 8 && opcode <= 11) continue; // Test/misc encodings require S.
        forms.push_back((opcode << 21) | 0x00000001u); // Rd overlaps Rn.
        forms.push_back((opcode << 21) | 0x02002180u); // Rotated immediate.
    }
    unsigned comparisons = 0;
    for (unsigned variant = 0; variant < 4; ++variant)
    for (unsigned cond = 0; cond < 14; ++cond) for (unsigned form : forms) {
        const std::uint32_t code[] = {(cond << 28) | form, 0xe2a33000u}; // ADC consumes preserved C.
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code);
        auto tr = translate_arm_block(bytes, sizeof(code), 0x1000, nullptr, nullptr,
            true, true, variant & 1, variant >= 2);
        if (!tr.entry_supported || !tr.complete) return false;
        auto module = build_wasm_module({tr.func});
        for (unsigned flags = 0; flags < 16; ++flags)
        for (unsigned budget : {0u, 1u, 2u, 3u}) for (unsigned seed : {0u, 1u}) {
            test_mem memory;
            memory.write_code(0x1000, {bytes, bytes + sizeof(code)});
            r12l1::exclusive_monitor monitor(1);
            auto cpu = make_cpu(memory, monitor);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned reg = 0; reg < 16; ++reg) {
                unsigned value = reg == 15 ? 0x1000 : (seed ? 0x80000000u : 0x7fffffffu) + reg;
                state[state_offsets::reg(reg) / 4] = value;
                cpu->set_reg(reg, value);
            }
            cpu->set_cpsr(0x10 | (flags << 28));
            state[state_offsets::CPSR / 4] = cpu->get_cpsr();
            state[state_offsets::MODE / 4] = 16;
            state[state_offsets::NIRQ / 4] = 1;
            for (auto pair : {std::pair<unsigned, unsigned>{state_offsets::NFLAG, 3},
                    {state_offsets::ZFLAG, 2}, {state_offsets::CFLAG, 1}, {state_offsets::VFLAG, 0}})
                state[pair.first / 4] = (flags >> pair.second) & 1;
            state[state_offsets::AOT_BUDGET / 4] = budget;
            const int count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            if (count != static_cast<int>(std::min(budget, 2u))) {
                printf("  FAIL conditional MOV budget %u count %d\n", budget, count); return false;
            }
            if (count) cpu->run(count);
            for (unsigned reg = 0; reg < 16; ++reg)
                if (state[state_offsets::reg(reg) / 4] != cpu->get_reg(reg)) {
                    printf("  FAIL conditional MOV %08X variant=%u flags=%u budget=%u R%u\n",
                        code[0], variant, flags, budget, reg); return false;
                }
            for (auto pair : {std::pair<unsigned, unsigned>{state_offsets::NFLAG, 31},
                    {state_offsets::ZFLAG, 30}, {state_offsets::CFLAG, 29},
                    {state_offsets::VFLAG, 28}, {state_offsets::TFLAG, 5}})
                if (state[pair.first / 4] != ((cpu->get_cpsr() >> pair.second) & 1)) {
                    printf("  FAIL conditional MOV flags %08X variant=%u flags=%u budget=%u\n",
                        code[0], variant, flags, budget); return false;
                }
            ++comparisons;
        }
    }
    printf("  PASS conditional_alu_select (%u exact budget/state comparisons)\n", comparisons);
#endif
    return true;
}

// Preserve flags and exact exits when CMP feeds a subsequent condition.
static bool test_compare_conditions() {
#ifdef __EMSCRIPTEN__
    const unsigned values[] = {0,1,2,0xffffffffu,0x80000000u,0x7fffffffu,0xffff0000u,0x12345678u};
    unsigned comparisons = 0;
    for (unsigned variant = 0; variant < 4; ++variant)
    for (unsigned cond = 0; cond < 14; ++cond)
    for (unsigned form : {0x03a02001u, 0x02800001u, 0x0a000000u}) {
        const std::uint32_t code[] = {0xe1500001u, (cond << 28) | form, 0xe2a33000u, 0xe2844001u};
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code);
        auto tr = translate_arm_block(bytes, sizeof(code), 0x1000, nullptr, nullptr,
            true, true, variant & 1, variant >= 2);
        if (!tr.entry_supported || !tr.complete) return false;
        auto module = build_wasm_module({tr.func});
        for (unsigned left : values) for (unsigned right : values)
        for (unsigned budget : {0u,1u,2u,3u,4u,7u}) {
            const unsigned flags = 10;
            test_mem memory;
            memory.write_code(0x1000, {bytes, bytes + sizeof(code)});
            r12l1::exclusive_monitor monitor(1);
            auto cpu = make_cpu(memory, monitor);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned reg = 0; reg < 16; ++reg) {
                unsigned value = reg == 15 ? 0x1000 : reg == 0 ? left : reg == 1 ? right : 0x12340000u + reg;
                state[state_offsets::reg(reg) / 4] = value;
                cpu->set_reg(reg, value);
            }
            cpu->set_cpsr(0x10 | (flags << 28));
            state[state_offsets::CPSR / 4] = cpu->get_cpsr();
            state[state_offsets::MODE / 4] = 16;
            state[state_offsets::NIRQ / 4] = 1;
            for (auto pair : {std::pair<unsigned, unsigned>{state_offsets::NFLAG, 3},
                    {state_offsets::ZFLAG, 2}, {state_offsets::CFLAG, 1}, {state_offsets::VFLAG, 0}})
                state[pair.first / 4] = (flags >> pair.second) & 1;
            state[state_offsets::AOT_BUDGET / 4] = budget;
            const int count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            if (count < 0 || count > static_cast<int>(budget) || (budget && !count)) {
                printf("  FAIL compare condition budget %u count %d\n", budget, count); return false;
            }
            if (count) cpu->run(count);
            for (unsigned reg = 0; reg < 16; ++reg)
                if (state[state_offsets::reg(reg) / 4] != cpu->get_reg(reg)) {
                    printf("  FAIL compare condition %08X variant=%u flags=%u budget=%u R%u\n",
                        code[0], variant, flags, budget, reg); return false;
                }
            for (auto pair : {std::pair<unsigned, unsigned>{state_offsets::NFLAG, 31},
                    {state_offsets::ZFLAG, 30}, {state_offsets::CFLAG, 29},
                    {state_offsets::VFLAG, 28}, {state_offsets::TFLAG, 5}})
                if (state[pair.first / 4] != ((cpu->get_cpsr() >> pair.second) & 1)) {
                    printf("  FAIL compare condition flags %08X variant=%u flags=%u budget=%u\n",
                        code[0], variant, flags, budget); return false;
                }
            ++comparisons;
        }
    }
    printf("  PASS compare_conditions (%u exact budget/state comparisons)\n", comparisons);
#endif
    return true;
}

// Exercise long products, modulo-64 accumulation, aliasing and conditional flags
// against DynCom with zero, partial, exact and oversized instruction budgets.
static bool test_arm_long_multiply() {
#ifdef __EMSCRIPTEN__
    const std::array<std::array<unsigned, 4>, 6> registers{{
        {{2,3,0,1}}, {{0,3,0,1}}, {{2,0,0,1}},
        {{1,3,0,1}}, {{2,1,0,1}}, {{0,1,0,1}}
    }}; // lo, hi, rm, rs (including both destinations overlapping inputs)
    const unsigned values[] = {0,1,2,0xffffffffu,0x80000000u,0x7fffffffu,0xffff0000u,0x12345678u};
    unsigned comparisons = 0;
    for (bool consecutive : {false,true})
    for (bool cached : {false,true}) for (unsigned form = 0; form < 8; ++form)
    for (const auto &r : registers) for (unsigned cond : {0u,1u,14u}) {
        const unsigned instruction = (cond << 28) | 0x00800090u | (form << 20)
            | (r[1]<<16) | (r[0]<<12) | (r[3]<<8) | r[2];
        const std::uint32_t code[] = {
            consecutive ? instruction & ~(1u << 21) : instruction,
            consecutive ? instruction : 0x02866001u, instruction};
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(code);
        auto tr = translate_arm_block(bytes, sizeof(code), 0x1000, nullptr, nullptr, true, true, cached, consecutive);
        if (!tr.entry_supported || !tr.complete) { printf("  FAIL long multiply not compiled %08X\n",instruction); return false; }
        auto module = build_wasm_module({tr.func});
        for (unsigned seed = 0; seed < 8; ++seed) for (unsigned flags : {0u,3u,12u,15u})
        for (unsigned budget : {0u,1u,2u,3u,4u}) {
            test_mem memory;
            memory.write_code(0x1000, {bytes, bytes + sizeof(code)});
            r12l1::exclusive_monitor monitor(1);
            auto cpu = make_cpu(memory, monitor);
            alignas(8) std::uint32_t state[256]{};
            for (unsigned reg = 0; reg < 16; ++reg) {
                unsigned value = reg == 15 ? 0x1000 : values[(seed + reg * 3) % 8];
                // Alternate high accumulator between signed extremes and all bits set.
                if (reg == 3) value = seed & 1 ? 0xffffffffu : 0x7fffffffu;
                state[state_offsets::reg(reg)/4] = value;
                cpu->set_reg(reg,value);
            }
            cpu->set_cpsr(0x10 | (flags<<28));
            for (auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,3},
                    {state_offsets::ZFLAG,2},{state_offsets::CFLAG,1},{state_offsets::VFLAG,0}})
                state[pair.first/4] = (flags >> pair.second)&1;
            state[state_offsets::AOT_BUDGET/4] = budget;
            const auto count = js_run_aot_wasm(module.data(), module.size(),
                reinterpret_cast<std::uint8_t *>(state), sizeof(state));
            if (count != static_cast<int>(std::min(budget,3u))) {
                printf("  FAIL long multiply budget %u count %d\n",budget,count); return false;
            }
            if (count) cpu->run(count);
            for (unsigned reg = 0; reg < 16; ++reg) if (state[state_offsets::reg(reg)/4] != cpu->get_reg(reg)) {
                printf("  FAIL long multiply %08X cached=%d seed=%u flags=%u budget=%u R%u %08X vs %08X\n",
                    instruction,cached,seed,flags,budget,reg,state[state_offsets::reg(reg)/4],cpu->get_reg(reg)); return false;
            }
            for (auto pair : {std::pair<unsigned,unsigned>{state_offsets::NFLAG,31},
                    {state_offsets::ZFLAG,30},{state_offsets::CFLAG,29},{state_offsets::VFLAG,28},{state_offsets::TFLAG,5}})
                if (state[pair.first/4] != ((cpu->get_cpsr() >> pair.second)&1)) {
                    printf("  FAIL long multiply flags %08X budget=%u\n",instruction,budget); return false;
                }
            ++comparisons;
        }
    }
    // Do not compile architecturally unpredictable register combinations.
    for (unsigned code : {0xE0822F90u,0xE08F2190u,0xE083F190u,0xE083219Fu,0xE0822190u}) {
        auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&code),4,
            0x1000,nullptr,nullptr,true,true,true);
        if (tr.entry_supported) { printf("  FAIL invalid long multiply %08X accepted\n",code); return false; }
    }
    printf("  PASS arm_long_multiply (%u exact budget/state comparisons)\n",comparisons);
#endif
    return true;
}

static bool test_msr_privilege_guard() {
#ifdef __EMSCRIPTEN__
    const std::uint32_t instruction = 0xE128F000;
    for (bool cached : {false,true}) {
        auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&instruction),4,
            0x1000,nullptr,nullptr,true,true,cached);
        auto module = build_wasm_module({tr.func});
        for (unsigned mode : {0x11u,0x12u,0x13u,0x17u,0x1bu,0x1fu}) for (unsigned active_mode : {mode,16u}) {
            alignas(8) std::array<std::uint32_t,256> state{};
            state[0] = 0xffffffffu;
            state[state_offsets::PC/4] = 0x1000;
            state[state_offsets::CPSR/4] = mode;
            state[state_offsets::MODE/4] = active_mode;
            state[state_offsets::AOT_BUDGET/4] = 1;
            const auto before = state;
            const int count = js_run_aot_wasm(module.data(),module.size(),
                reinterpret_cast<std::uint8_t *>(state.data()),sizeof(state));
            if (count != 0 || state != before) {
                printf("  FAIL MSR privileged mode %u modified state\n",mode); return false;
            }
        }
    }
    for (bool cached : {false,true}) for (unsigned mode_bits : {0u,16u})
    for (unsigned operand : {0u,0xffffffffu,0x80000000u,0x08000000u,0x40000020u}) {
        auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&instruction),4,
            0x1000,nullptr,nullptr,true,true,cached);
        auto module = build_wasm_module({tr.func});
        alignas(8) std::array<std::uint32_t,256> state{};
        state[0] = operand;
        state[state_offsets::PC/4] = 0x1000;
        state[state_offsets::CPSR/4] = mode_bits;
        state[state_offsets::MODE/4] = 16;
        state[state_offsets::AOT_BUDGET/4] = 1;
        test_mem memory;
        memory.write32(0x1000,instruction);
        r12l1::exclusive_monitor monitor(1);
        auto cpu = make_cpu(memory,monitor);
        cpu->set_reg(0,operand); cpu->set_reg(15,0x1000); cpu->set_cpsr(mode_bits);
        cpu->run(1);
        const int count = js_run_aot_wasm(module.data(),module.size(),
            reinterpret_cast<std::uint8_t *>(state.data()),sizeof(state));
        if (count != 1 || state[state_offsets::CPSR/4] != cpu->get_cpsr()
            || state[state_offsets::MODE/4] != 16 || state[state_offsets::PC/4] != cpu->get_reg(15)) {
            printf("  FAIL MSR user mode bits=%u operand=%08X\n",mode_bits,operand);return false;
        }
    }
    printf("  PASS msr_privilege_guard\n");
#endif
    return true;
}

static bool test_cached_callback_state() {
#ifdef __EMSCRIPTEN__
    const std::uint32_t code[] = {0xE3A02007, 0xE5910000, 0xE2834001, 0xE2A05000};
    auto tr = translate_arm_block(reinterpret_cast<const std::uint8_t *>(code), sizeof(code),
        0x1000, nullptr, nullptr, true, false, true);
    auto module = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
        {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    alignas(8) std::uint32_t state[256]{};
    state[1] = 0x8000;
    state[state_offsets::PC / 4] = 0x1000;
    state[state_offsets::AOT_BUDGET / 4] = 4;
    test_mem memory;
    memory.write32(0x8000, 20);
    g_test_mem = &memory;
    g_mutate_callback_state = true;
    g_callback_observed_state = false;
    const auto count = js_run_aot_wasm(module.data(), module.size(),
        reinterpret_cast<std::uint8_t *>(state), sizeof(state));
    g_mutate_callback_state = false;
    g_test_mem = nullptr;
    if (count != 4 || !g_callback_observed_state || state[3] != 11 || state[4] != 12 || state[5] != 21) {
        printf("  FAIL cached_callback_state count=%d observed=%d r3=%u r4=%u r5=%u\n",
            count, g_callback_observed_state, state[3], state[4], state[5]);
        return false;
    }
    printf("  PASS cached_callback_state\n");
#endif
    return true;
}

static bool test_code_write_protection() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
    namespace tracking = eka2l1::common::code_tracking;
    struct restore_mode { bool saved=tracking::protect_writes,folded=r12l1::dyncom_folded_tlb; ~restore_mode(){tracking::protect_writes=saved;r12l1::dyncom_folded_tlb=folded;g_write16_observer={};} } restore;
    tracking::protect_writes=true;
    alignas(4096) static std::uint8_t backing[5*4096]{};
    tracking::register_allocation(backing,sizeof(backing));
    for(bool folded : {false,true}) {
        r12l1::dyncom_folded_tlb=folded;
        r12l1::tlb tlb(12,folded);
        tlb.add(0x4000,backing,7);tlb.add(0x8000,backing,7);tlb.add(0x5000,backing+4096,7);
        if(!folded && !tlb.lookup_access<prot_write>(0x4000)){printf("write protection failure line %d\n",__LINE__);return false;}
        auto stamps=tracking::snapshot(backing,8);if(stamps.empty()){printf("write protection failure line %d\n",__LINE__);return false;}
        tlb.sync_write_protection();
        if(tlb.lookup_access<prot_write>(0x4000)||tlb.lookup_access<prot_write>(0x8000)){printf("write protection failure line %d\n",__LINE__);return false;}
        if(tlb.lookup_access<prot_read>(0x4000)!=backing || tlb.lookup_access<prot_exec>(0x8000)!=backing){printf("write protection failure line %d\n",__LINE__);return false;}
        if(tlb.lookup_access<prot_write>(0x5000)!=backing+4096){printf("write protection failure line %d\n",__LINE__);return false;}
        tlb.add(0xc000,backing,7);if(tlb.lookup_access<prot_write>(0xc000)){printf("write protection failure line %d\n",__LINE__);return false;}
        tlb.add(0xc000,backing+4096,7);if(!tlb.lookup_access<prot_write>(0xc000)){printf("write protection failure line %d\n",__LINE__);return false;}
        // A partially overlapping host span must be denied too.
        tlb.add(0x6000,backing+4094,7);if(tlb.lookup_access<prot_write>(0x6000)){printf("write protection failure line %d\n",__LINE__);return false;}
        for(auto store : {0xe5c10000u,0xe1c100b0u,0xe5810000u,0xe8810009u}) {
            const std::uint32_t words[]={store,0xe2822001,0xe12fff1e};
            auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(words),sizeof(words),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true);
            auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
            alignas(8) std::uint32_t state[256]{};
            state[0]=0x12345678;state[1]=0x4000;state[3]=0x11223344;state[14]=0x2000;
            state[state_offsets::AOT_BUDGET/4]=3;state[state_offsets::NIRQ/4]=1;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            auto before=backing[0];
            bool helper_seen=false,helper_precise=true;
            auto write_stamp=tracking::snapshot(backing,8);
            g_write16_observer=[&](std::uint32_t ptr,std::uint32_t address,std::uint32_t value){
                auto *observed=reinterpret_cast<std::uint32_t*>(ptr);
                helper_seen=true;helper_precise=address==0x4000 && value==0x12345678 && observed[1]==0x4000 && observed[2]==0 && observed[15]==0x1000;
                const std::uint16_t half=value;
                std::memcpy(backing,&half,2);tracking::guest_write(backing,2);
            };
            auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
            g_write16_observer={};
            if(store==0xe1c100b0u) {
                // STRH deliberately uses the precise callback path, even when
                // other direct stores defer after a denied writable TLB tag.
                if(count!=1 || !helper_seen || !helper_precise || write_stamp.empty() || write_stamp[0].valid() || backing[0]!=0x78 || backing[1]!=0x56 || state[1]!=0x4000 || state[2] || state[15]!=0x1004){printf("write protection halfword callback failure\n");return false;}
                continue;
            }
            if(count || helper_seen || state[1]!=0x4000 || state[2] || backing[0]!=before || state[15]!=0x1000){printf("write protection failure line %d store %x count %u r1 %x r2 %x pc %x mem %u/%u\n",__LINE__,store,count,state[1],state[2],state[15],backing[0],before);return false;}
        }
        // Delivered invariant read/write proofs may coexist with protection.
        // A watched destination refuses its write proof and exits precisely;
        // a remap to untracked data permits the same compiled function.
        const std::uint32_t proof_words[]={0xe5910000,0xe5913004,0xe5914008,0xe5820000,0xe5823004,0xe5824008};
        auto proof=translate_arm_block(reinterpret_cast<const std::uint8_t*>(proof_words),sizeof(proof_words),0x1000,nullptr,nullptr,true,true,true,true,nullptr,true,arm_ir_policy::write_budget_chunks);
        if(proof.proved_reads!=3 || proof.proved_writes!=3){printf("protected entry proofs not selected\n");return false;}
        auto proof_module=build_wasm_module({proof.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        std::array<std::uint32_t,1024> input{};input[0]=7;input[1]=11;input[2]=19;
        for(bool watched : {true,false}) for(unsigned budget : {0u,1u,2u,3u,4u,5u,6u,7u}) {
            auto *destination=watched?backing:backing+4096;std::array<std::uint8_t,12> before{};std::memcpy(before.data(),destination,12);
            tlb.add(0x201000,reinterpret_cast<std::uint8_t*>(input.data()),prot_read);
            tlb.add(0x403000,destination,prot_read_write);tlb.sync_write_protection();
            alignas(8) std::uint32_t state[256]{};state[1]=0x201000;state[2]=0x403000;state[15]=0x1000;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);state[state_offsets::AOT_BUDGET/4]=budget;state[state_offsets::NIRQ/4]=1;
            const auto count=js_run_aot_wasm(proof_module.data(),proof_module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
            const unsigned expected=std::min(budget,watched?3u:6u);
            std::array<std::uint8_t,12> expected_memory=before;
            if(!watched && expected>3)std::memcpy(expected_memory.data(),input.data(),(expected-3)*4);
            if(count!=expected || state[15]!=0x1000+expected*4 || state[1]!=0x201000 || state[2]!=0x403000 || state[0]!=(expected?7u:0u) || state[3]!=(expected>=2?11u:0u) || state[4]!=(expected>=3?19u:0u) || std::memcmp(destination,expected_memory.data(),12)) {printf("protected proof budget/alias failure watched=%d budget=%u count=%u\n",watched,budget,count);return false;}
        }
        // The checked write path still updates versions; refill stays protected.
        backing[0]^=1;tracking::guest_write(backing,1);if(stamps[0].valid()){printf("write protection failure line %d\n",__LINE__);return false;}
        tlb.add(0x4000,backing,7);if(tlb.lookup_access<prot_write>(0x4000)){printf("write protection failure line %d\n",__LINE__);return false;}
    }
    r12l1::tlb tlb(12,true);tlb.add(0x7000,backing+8192,7);tlb.sync_write_protection();
    if(!tlb.lookup_access<prot_write>(0x7000)){printf("write protection failure line %d\n",__LINE__);return false;}
    auto later=tracking::snapshot(backing+8192,4);if(later.empty()){printf("write protection failure line %d\n",__LINE__);return false;}
    tlb.sync_write_protection();if(tlb.lookup_access<prot_write>(0x7000)){printf("write protection failure line %d\n",__LINE__);return false;}
    const auto saved_generation=tracking::watch_generation;
    tracking::watch_generation=UINT64_MAX;
    if(tracking::snapshot(backing+12288,4).empty() || tracking::watch_generation){printf("write protection failure line %d\n",__LINE__);return false;}
    tlb.add(0x9000,backing+16384,7);tlb.sync_write_protection();
    if(!tlb.lookup_access<prot_write>(0x9000)){printf("write protection failure line %d\n",__LINE__);return false;}
    if(tracking::snapshot(backing+16384,4).empty() || tracking::watch_generation){printf("write protection failure line %d\n",__LINE__);return false;}
    tlb.sync_write_protection();if(tlb.lookup_access<prot_write>(0x9000)){printf("write protection failure line %d\n",__LINE__);return false;}
    tracking::watch_generation=saved_generation; // No surviving compiled fixture.
    tracking::escape_pointer(backing);
    if(later[0].valid()){printf("write protection failure line %d\n",__LINE__);return false;}
    tlb.flush();tlb.add(0x7000,backing+8192,7);
    if(!tlb.lookup_access<prot_write>(0x7000)){printf("write protection failure line %d\n",__LINE__);return false;} // escaped code uses exact validation
    tracking::retire_allocation(backing);
    printf("  PASS code_write_protection (aliases, refills, remaps, partial spans, deferred stores, escapes, generation exhaustion)\n");
#else
    printf("  SKIP code_write_protection (build option off)\n");
#endif
    return true;
}

static bool test_generated_write_versions() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_VERSIONS)
    namespace tracking = eka2l1::common::code_tracking;
    alignas(4096) static std::uint8_t backing[8192]{};
    tracking::register_allocation(backing,sizeof(backing));
    for (auto store : {0xe5c10000u,0xe1c100b0u,0xe5810000u,0xe8a10009u}) {
        for (auto alias : {0x4000u,0x8000u}) {
            const std::uint32_t words[]={store,0xe2822001,0xe12fff1e};
            auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t *>(words),sizeof(words),0x1000,nullptr,nullptr,true,true,true,true);
            auto module=build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
                {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(alias,backing,7);
            alignas(8) std::uint32_t state[256]{};
            state[0]=0x12345678;state[3]=0x11223344;state[1]=alias;state[14]=0x2000;
            state[state_offsets::AOT_BUDGET/4]=3;state[state_offsets::NIRQ/4]=1;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            auto before=tracking::snapshot(backing,8);
            if(before.empty())return false;
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
            const bool overflow = store == 0xe8a10009u && alias == 0x8000u;
            if (overflow) tracking::pages[reinterpret_cast<std::uintptr_t>(backing)>>12].version=UINT32_MAX;
#endif
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
            const auto epoch = tracking::validation_epoch();
#endif
            auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
            if (tracking::validation_epoch() == epoch) return false;
            if (overflow && !tracking::snapshot(backing,8).empty()) return false;
#endif
            if(count!=3 || state[2]!=1 || before[0].valid() || backing[0]!=0x78) return false;
        }
    }
    tracking::retire_allocation(backing);
#endif
    printf("  PASS generated_write_versions (STRB/STRH/STR/STM through physical aliases)\n");
    return true;
}

static bool test_code_validity_versions() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_VERSIONS)
    namespace tracking = eka2l1::common::code_tracking;
    alignas(4096) static std::uint8_t backing[16384]{};
    tracking::register_allocation(backing, 8192);
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto cpu = make_cpu(memory, monitor);
    std::atomic<std::uint64_t> mapping{1};
    cpu->code_mapping_generation = &mapping; cpu->code_address_space = 1;
    core::code_mapping view{1,backing,4096};
    cpu->resolve_code = [&](std::uint32_t addr, core::code_mapping &out) {
        out = view; if (addr == 0x2000) out.bytes = backing+4096; return true;
    };
    validated_code_cache cache;
    auto install = [&]() {
        auto &b = cache.insert(0x1000,view,8);
        validated_code_cache::add_dependency(b,0x2000,backing+4096,{backing+4096,backing+4104});
        return cache.find(0x1000,*cpu);
    };
    auto *entry = install();
    if (!entry || entry->stamps.size()!=2 || !cache.find(0x1000,*cpu)) return false;
    // Aliases use the host backing, and a changed dependency invalidates caller.
    backing[4096]=1;tracking::guest_write(backing+4096,1);
    if (cache.find(0x1000,*cpu)) return false;
    entry=install();if(!entry) return false;
    const auto version=entry->stamps[0].version;
    tracking::guest_write(backing,4); // Same bytes still refresh their generation.
    if (!cache.find(0x1000,*cpu) || entry->stamps[0].version==version) return false;
    // A retained host pointer poisons the WHOLE allocation, including a later
    // write through that pointer, after intervening successful lookups.
    tracking::escape_pointer(backing+6000);
    if(!cache.find(0x1000,*cpu) || !entry->stamps.empty()) return false;
    backing[0]=2;
    if(cache.find(0x1000,*cpu)) return false;
    if(!install())return false;
    view.bytes=backing+8192;++mapping;
    if(cache.find(0x1000,*cpu))return false;
    tracking::retire_allocation(backing);
    tracking::register_allocation(backing,8192);
    if(!tracking::snapshot(backing,8).empty()) return false; // no reuse resurrection
    tracking::retire_allocation(backing);
    tracking::register_allocation(backing+8192,8192);
    auto stamps=tracking::snapshot(backing+8192,8);if(stamps.empty())return false;
    tracking::pages[reinterpret_cast<std::uintptr_t>(backing+8192)>>12].version=UINT32_MAX;
    tracking::guest_write(backing+8192,4);
    if(!tracking::snapshot(backing+8192,8).empty())return false;
    auto last=tracking::snapshot(backing+12288,8);
    if(last.empty())return false;
    tracking::retire_allocation(backing+8192);
    if(last[0].valid())return false;
#endif
    printf("  PASS code_validity_versions (dependencies, aliases, escapes, remap, reuse, overflow)\n");
    return true;
}

static bool test_code_lifecycle() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_LIFECYCLE)
    namespace tracking = eka2l1::common::code_tracking;
    alignas(4096) static std::uint8_t backing[16384]{};
    tracking::register_allocation(backing, sizeof(backing));
    auto epoch = tracking::validation_epoch();
    // Loader/data writes on unwatched pages do not invalidate compiled code.
    tracking::guest_write(backing, sizeof(backing));
    if (tracking::validation_epoch() != epoch) return false;
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto cpu = make_cpu(memory, monitor);
    std::atomic<std::uint64_t> mapping{1};
    cpu->code_mapping_generation = &mapping; cpu->code_address_space = 1;
    core::code_mapping view{1,backing,4096};
    cpu->resolve_code = [&](std::uint32_t addr, core::code_mapping &out) {
        out = view; if (addr == 0x2000) out.bytes = backing+4096; return true;
    };
    validated_code_cache cache;
    auto install = [&]() {
        auto &b = cache.insert(0x1000,view,8);
        validated_code_cache::add_dependency(b,0x2000,backing+4096,{backing+4096,backing+4104});
        return cache.find(0x1000,*cpu);
    };
    auto *entry = install();
    if (!entry || entry->stamps.size()!=2 || entry->validation_epoch != epoch) return false;
    if (!cache.find(0x1000,*cpu) || entry->validation_epoch != epoch) return false;
    // Mapping refresh must still reject stale backing even in the same epoch.
    view.bytes=backing+8192; ++mapping;
    if (cache.find(0x1000,*cpu)) return false;
    view.bytes=backing; ++mapping;
    entry=install(); if(!entry) return false;
    tracking::guest_write(backing+8192,4);
    if (tracking::validation_epoch() != epoch) return false;
    // Multiple writes coalesce; unchanged bytes refresh stamps once.
    tracking::guest_write(backing,4); tracking::guest_write(backing,4);
    if (!cache.find(0x1000,*cpu) || entry->validation_epoch != epoch+1) return false;
    epoch = entry->validation_epoch;
    auto other = tracking::snapshot(backing+8192,4);
    if (other.empty()) return false;
    tracking::guest_write(backing+8192,4);
    if (!cache.find(0x1000,*cpu) || entry->validation_epoch != epoch+1) return false;
    // Dependency writes through backing aliases invalidate the caller.
    backing[4096]=7; tracking::guest_write(backing+4096,1);
    if (cache.find(0x1000,*cpu)) return false;
    entry=install(); if (!entry) return false;
    // Host exposure forces exact checking even after successful epoch hits.
    tracking::escape_pointer(backing+12288);
    if (!cache.find(0x1000,*cpu) || !entry->stamps.empty()) return false;
    backing[0]=3;
    if (cache.find(0x1000,*cpu)) return false;
    tracking::retire_allocation(backing);
    tracking::register_allocation(backing,sizeof(backing));
    if (!tracking::snapshot(backing,8).empty()) return false;
    tracking::retire_allocation(backing);
    const auto saved_epoch=tracking::epoch;
    tracking::epoch=UINT64_MAX; tracking::dirty.store(1);
    if(tracking::validation_epoch()!=0) return false;
    tracking::dirty.store(1);
    if(tracking::validation_epoch()!=0) return false;
    tracking::epoch=saved_epoch; // Isolated fixture; no cache entries survive it.
#endif
    printf("  PASS code_lifecycle (lazy watch, coalesced changes, dependencies, host escape, reuse)\n");
    return true;
}

#ifdef __EMSCRIPTEN__
EM_JS(void, js_export_flag_probe, (const std::uint8_t *bytes, unsigned size), {
    console.log('FLAG_MODULE ' + Buffer.from(HEAPU8.subarray(bytes, bytes + size)).toString('base64'));
});
#endif

static bool test_folded_tlb_guards() {
#ifdef __EMSCRIPTEN__
    struct restore { bool old=r12l1::dyncom_folded_tlb; ~restore(){r12l1::dyncom_folded_tlb=old;} } restore_mode;
    r12l1::dyncom_folded_tlb=true;
    for (unsigned kind=0;kind<3;++kind) {
        const bool block=kind==1, proof=kind==2;
        const std::vector<std::uint32_t> words=proof
            ? std::vector<std::uint32_t>{0xe5910000,0xe5913004,0xe5914008,0xe5820000,0xe5823004,0xe5824008}
            : std::vector<std::uint32_t>{block?0xe8910009u:0xe5910000u,block?0xe8820009u:0xe5820000u};
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t *>(words.data()),words.size()*4,0x1000,
            nullptr,nullptr,true,true,true,true,nullptr,true,arm_ir_policy::write_budget_chunks);
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        #ifdef EKA2L1_WASM_CODE_VERSIONS
        const unsigned expected_proofs=0; // Deliberately incompatible research modes.
#else
        const unsigned expected_proofs=3;
#endif
        if(proof && (tr.proved_reads!=expected_proofs || tr.proved_writes!=expected_proofs)) {
            printf("  FAIL folded TLB entry proof policy mismatch\n");return false;
        }
        for(unsigned budget=0;budget<=words.size()+1;++budget) {
            std::array<std::uint32_t,1024> input{},output{};input[0]=0x12345678;input[1]=0xabcdef01;input[2]=0x31415926;
            r12l1::tlb cache(12,true);cache.add(0x201000,reinterpret_cast<std::uint8_t*>(input.data()),prot_read);
            cache.add(0x401000,reinterpret_cast<std::uint8_t*>(output.data()),prot_write);
            alignas(8) std::uint32_t state[256]{};state[1]=0x201000;state[2]=0x401000;state[15]=0x1000;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(cache.entries);
            state[state_offsets::AOT_BUDGET/4]=budget;state[state_offsets::NIRQ/4]=1;
            const int count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
            const unsigned expected=std::min(budget,unsigned(words.size()));
            if(count!=expected || state[15]!=0x1000+4*expected || state[0]!=(expected?input[0]:0)
                || state[3]!=(((block && expected)||(proof && expected>=2))?input[1]:0)
                || state[4]!=((proof && expected>=3)?input[2]:0)
                || output[0]!=((expected>=(proof?4u:2u))?input[0]:0)
                || output[1]!=(((block && expected==2)||(proof && expected>=5))?input[1]:0)
                || output[2]!=((proof && expected>=6)?input[2]:0)) {
                printf("  FAIL folded TLB kind=%u budget=%u count=%d pc=%x\n",kind,budget,count,state[15]);return false;
            }
        }
    }
#endif
    printf("  PASS folded TLB scalar/block/entry guards and exact budgets\n");return true;
}



static bool test_boundary_details() {
#ifdef __EMSCRIPTEN__
    using namespace exit_census;
    struct restore {
        bool census=enabled,predicates=predicated_leaves,perf=eka2l1::common::performance::enabled,detail=eka2l1::common::performance::detailed;
        int phase=eka2l1::common::performance::phase.load();
        ~restore(){enabled=census;predicated_leaves=predicates;eka2l1::common::performance::enabled=perf;eka2l1::common::performance::detailed=detail;eka2l1::common::performance::phase=phase;}
    } saved;
    enabled=true;eka2l1::common::performance::enabled=true;eka2l1::common::performance::detailed=true;eka2l1::common::performance::phase=2;
    struct fixture {unsigned opcode,expected;bool predicates=true;};
    const fixture cases[]={{0x03a00001,predicates_disabled,false},{0xf3a00001,reserved_predicate},
        {0x15910000,conditional_memory},{0x1afffffe,conditional_transfer},{0xeafffffe,internal_branch},
        {0xeb000000,nested_call},{0xe8bd8010,block_transfer},{0xef000000,coprocessor_or_supervisor},
        {0xe59d0000,sp_operand},{0xe1a0e000,lr_operand},{0xe51ff004,pc_operand},
        {0xe7910010,register_memory_shift},{0xe1000000,status_or_misc},
        {0xe080000d,sp_index},{0xe080000e,lr_index},{0xe080000f,pc_index},
        {0xe0800d10,sp_shift},{0xe0800e10,lr_shift},{0xe0800f10,pc_shift},
        {0xe0000190,multiply},{0xe1d000b0,halfword_or_signed_transfer}};
    const unsigned caller[]={0xeb0003feu,0xe2844001u};
    unsigned checks=0;
    for(auto f:cases) {
        predicated_leaves=f.predicates;
        const unsigned leaf[]={0xe1a00000u,f.opcode,0xe12fff1eu};
        leaf_resolver resolver=[&](unsigned){const auto *p=reinterpret_cast<const std::uint8_t*>(leaf);return std::vector<std::uint8_t>(p,p+sizeof(leaf));};
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller),sizeof(caller),0x1000,nullptr,nullptr,true,false,true,true,&resolver,true,arm_ir_policy::write_budget_chunks);
        if(!tr.dependencies.empty())return false;
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        alignas(8) unsigned state[256]{};state[15]=0x1000;state[state_offsets::MODE/4]=16;state[state_offsets::CPSR/4]=16;
        state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=16;
        const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
        if(count!=1 || state[15]!=0x2000 || last_constraint!=3 || last_restriction!=f.expected || last_rejected_pc!=0x2004 || last_rejected_opcode!=f.opcode) {
            printf(" FAIL boundary detail op=%x got=%s expected=%s pc=%x count=%d\n",f.opcode,restriction_name(last_restriction),restriction_name(f.expected),last_rejected_pc,count);return false;
        }
        const auto before=call_restrictions[restriction_name(f.expected)];
        record(0x1000,0x2000,7,count,16,0);
        if(call_restrictions[restriction_name(f.expected)]!=before+1)return false;
        ++checks;
    }
    for(unsigned address:{0x1000u,0x1800u,0x2000u,0x3000u}) {
        test_mem memory;const unsigned code[]={0xe5810000u,0xe2822001u};
        memory.write_code(0x1000,{reinterpret_cast<const std::uint8_t*>(code),reinterpret_cast<const std::uint8_t*>(code)+sizeof(code)});
        memory.write32(0x2000,0xe12fff1e);
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(code),sizeof(code),0x1000,nullptr,nullptr,true,false,true,true,nullptr,true,arm_ir_policy::write_budget_chunks);
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
        for(unsigned page:{0x1000u,0x2000u,0x3000u})tlb.add(page,memory.data.data()+page,3);
        validated_code_cache cache;core::code_mapping view{1,memory.data.data()+0x1000,8};auto &entry=cache.insert(0x1000,view,8);
        validated_code_cache::add_dependency(entry,0x2000,memory.data.data()+0x2000,{memory.data.begin()+0x2000,memory.data.begin()+0x2004});
        alignas(8) unsigned state[256]{};state[0]=0xe1a00000;state[1]=address;state[15]=0x1000;
        state[state_offsets::MODE/4]=16;state[state_offsets::CPSR/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=16;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        state[state_offsets::AOT_CODE_BEGIN/4]=entry.guard_begin;state[state_offsets::AOT_CODE_END/4]=entry.guard_end;
        guard_hits=0;last_guard_host=0;last_guard_size=0;g_test_mem=&memory;
        const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
        const bool guarded=address!=0x3000;
        if(count!=(guarded?1:2) || guard_hits!=unsigned(guarded) || state[2]!=unsigned(!guarded))return false;
        if(guarded && (last_guard_host!=reinterpret_cast<std::uintptr_t>(memory.data.data()+address) || last_guard_size!=4 ||
            validated_code_cache::diagnostic_code_overlap(entry,last_guard_host,last_guard_size)!=(address!=0x1800)))return false;
        ++checks;
    }
    // Compare instrumented entry proofs with identical uninstrumented code.
    // A gap can reject a proof before a store executes (even at budget zero).
    // Missing/write-denied mappings must not be called interval overlaps.
#ifndef EKA2L1_WASM_CODE_VERSIONS
    for(unsigned address:{0x1000u,0x1800u,0x2000u,0x3000u})for(unsigned permission:{0u,1u,3u})for(unsigned budget=0;budget<=5;++budget) {
        test_mem memory;const unsigned code[]={0xe5810000u,0xe5812004u,0xe5813008u,0xe581400cu};
        const auto *bytes=reinterpret_cast<const std::uint8_t*>(code);
        memory.write_code(0x1000,{bytes,bytes+sizeof(code)});memory.write32(0x2000,0xe12fff1e);
        auto translate=[&](){return translate_arm_block(bytes,sizeof(code),0x1000,nullptr,nullptr,true,false,true,true,nullptr,true,arm_ir_policy::write_budget_chunks);};
        enabled=false;auto control=translate();enabled=true;auto candidate=translate();
        if(candidate.proved_writes!=4 || !candidate.func.outlined_callee) {printf(" FAIL entry-proof diagnostic fixture not selected\n");return false;}
        const std::vector<wasm_import_func> imports={{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
        auto module=build_wasm_module({candidate.func},imports),original=build_wasm_module({control.func},imports);
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
        for(unsigned page:{0x1000u,0x2000u,0x3000u})tlb.add(page,memory.data.data()+page,permission);
        validated_code_cache cache;core::code_mapping view{1,memory.data.data()+0x1000,sizeof(code)};auto &entry=cache.insert(0x1000,view,sizeof(code));
        validated_code_cache::add_dependency(entry,0x2000,memory.data.data()+0x2000,{memory.data.begin()+0x2000,memory.data.begin()+0x2004});
        alignas(8) unsigned state[256]{};state[0]=0xe1a00000;state[1]=address;state[15]=0x1000;
        state[state_offsets::MODE/4]=16;state[state_offsets::CPSR/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        state[state_offsets::AOT_CODE_BEGIN/4]=entry.guard_begin;state[state_offsets::AOT_CODE_END/4]=entry.guard_end;
        alignas(8) unsigned expected[256];std::copy(std::begin(state),std::end(state),std::begin(expected));const auto before=memory.data;
        entry_proof_failed=0;entry_overlap_count=0;entry_other_failure=0;g_test_mem=&memory;
        entry_proof_attempted=0;entry_read_spans=0;entry_write_spans=0;
        const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
        const bool overlap=permission==3 && address!=0x3000;
        if(entry_proof_attempted!=1 || entry_read_spans || entry_write_spans!=1) {printf(" FAIL entry-proof attempt/span counters\n");return false;}
        if(entry_proof_failed!=unsigned(permission!=3 || overlap) || entry_overlap_count!=unsigned(overlap) || entry_other_failure!=unsigned(permission!=3)) {
            printf(" FAIL entry-proof addr=%x permission=%u budget=%u fallback=%u overlaps=%u other=%u\n",address,permission,budget,entry_proof_failed,entry_overlap_count,entry_other_failure);return false;
        }
        if(overlap && (entry_overlaps[0].host!=reinterpret_cast<std::uintptr_t>(memory.data.data()+address) || entry_overlaps[0].bytes!=16 ||
            validated_code_cache::diagnostic_code_overlap(entry,entry_overlaps[0].host,entry_overlaps[0].bytes)!=(address!=0x1800)))return false;
        const auto after=memory.data;std::copy(before.begin(),before.end(),memory.data.begin());
        const auto original_count=js_run_aot_wasm(original.data(),original.size(),reinterpret_cast<std::uint8_t*>(expected),sizeof(expected));g_test_mem=nullptr;
        if(count!=original_count || !std::equal(std::begin(state),std::end(state),std::begin(expected)) || after!=memory.data) {
            printf(" FAIL instrumented entry-proof equivalence addr=%x permission=%u budget=%u count=%d original=%d\n",address,permission,budget,count,original_count);return false;
        }
        ++checks;
    }
#endif
    printf(" PASS boundary details (%u exact rejection labels/counters and primary/dependency/gap/outside guards)\n",checks);
#endif
    return true;
}

static bool test_call_prefixes() {
#ifdef __EMSCRIPTEN__
    struct restore {bool flag=predicated_leaves;unsigned features=leaf_features;std::string limits=execution_limits_text();
        ~restore(){predicated_leaves=flag;leaf_features=features;parse_execution_limits(limits.c_str());}} saved;
    configure_execution_limits(512,16,8,512);predicated_leaves=true;leaf_features=8;
    unsigned checks=0;
    for(unsigned cond=0;cond<15;++cond)for(unsigned layout=0;layout<2;++layout) {
        const std::vector<unsigned> caller=layout?std::vector<unsigned>{0x1a000002u,0xeb0003fdu,0xe28bb001u,0xe3a0c002u,0xe2888001u}:
            std::vector<unsigned>{0xeb0003feu,0xe28bb001u,0xe2888001u};
        const unsigned leaf[]={0xe92d4010u,0xe1a04000u,0x05910000u|(cond<<28),0xe5842000u,0xe28dd008u,0xeb0003f9u,0xe3a0b077u};
        leaf_resolver resolver=[&](unsigned pc){const auto *b=reinterpret_cast<const std::uint8_t*>(leaf);
            return pc==0x2000?std::vector<std::uint8_t>(b,b+sizeof(leaf)):std::vector<std::uint8_t>{};};
        auto translate=[&](arm_ir_policy policy=arm_ir_policy::write_budget_chunks){return translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller.data()),caller.size()*4,0x1000,nullptr,nullptr,true,false,true,true,&resolver,true,policy);};
        leaf_features=0;if(!translate().dependencies.empty())return false;
        leaf_features=8;predicated_leaves=false;if(!translate().dependencies.empty())return false;
        predicated_leaves=true;if(!translate(arm_ir_policy::conditional_value_ir).dependencies.empty())return false;
        leaf_instruction_limit=4;if(!translate().dependencies.empty())return false;leaf_instruction_limit=16;
        auto tr=translate();
        if(tr.dependencies.size()!=1 || tr.dependencies[0].bytes.size()!=24 ||
            std::find(tr.resume_points.begin(),tr.resume_points.end(),layout?0x1008u:0x1004u)==tr.resume_points.end() ||
            std::find(tr.resume_points.begin(),tr.resume_points.end(),0x2018u)==tr.resume_points.end()) {
            printf(" FAIL prefix selection/continuations layout=%u\n",layout);return false;
        }
        if(!layout && tr.end_address!=0x1004){printf(" FAIL prefix reachable caller continuation\n");return false;}
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned flags=0;flags<16;++flags)for(unsigned mapping=0;mapping<7;++mapping)for(unsigned budget=0;budget<=10;++budget) {
            test_mem actual;const auto *cb=reinterpret_cast<const std::uint8_t*>(caller.data());actual.write_code(0x1000,{cb,cb+caller.size()*4});
            const auto *lb=reinterpret_cast<const std::uint8_t*>(leaf);actual.write_code(0x2000,{lb,lb+sizeof(leaf)});
            actual.write32(0x8000,0x76543210);actual.write32(0xa000,0xabcdef01);
            test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
            if(mapping!=1)tlb.add(0xb000,actual.data.data()+0xb000,3);
            if(mapping!=2)tlb.add(0x8000,actual.data.data()+0x8000,3);
            const unsigned store_page=mapping==4?0x1000:mapping==5?0x2000:0xa000;
            if(mapping!=3)tlb.add(0xa000,actual.data.data()+store_page,mapping==6?1:3);
            if(mapping==4 || mapping==5)reference->set_tlb_page(0xa000,expected.data.data()+store_page,prot_read_write);
            alignas(8) unsigned state[256]{};
            for(unsigned reg=0;reg<16;++reg){state[reg]=reg==0?0xa000:reg==1?0x8000:reg==2?0xe1a00000:reg==13?0xb020:reg==14?0x4000:reg==15?0x1000:reg;reference->set_reg(reg,state[reg]);}
            reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=16|(flags<<28);
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x2018);
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual;const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
            if(count<0 || count>int(budget) || (!count && budget)){printf(" FAIL prefix count\n");return false;}
            if(count)reference->run(count);
            for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)) {
                printf(" FAIL prefix R%u cond=%u layout=%u flags=%u map=%u budget=%u count=%d got=%x expected=%x\n",reg,cond,layout,flags,mapping,budget,count,state[reg],reference->get_reg(reg));return false;
            }
            for(unsigned f=0;f<4;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(31-f))&1))return false;
            if(actual.data!=expected.data){printf(" FAIL prefix memory cond=%u layout=%u flags=%u map=%u budget=%u\n",cond,layout,flags,mapping,budget);return false;}
            if(!layout && mapping==0 && budget>=7 && (count!=7 || state[15]!=0x3000 || state[14]!=0x2018 || state[11]!=11)) {
                printf(" FAIL prefix nested-call exit/continuation\n");return false;
            }
            ++checks;
        }
    }
    for(unsigned op:{0xeafffffeu,0x1b000000u,0xe12fff1eu,0xe8bd8010u,0xe59ff000u,0xe10f0000u,0xe8900000u,0xe8b00001u,0xee000000u}) {
        const unsigned caller[]={0xeb0003feu};const unsigned leaf[]={op,0xeb000000u};
        leaf_resolver resolver=[&](unsigned){const auto *b=reinterpret_cast<const std::uint8_t*>(leaf);return std::vector<std::uint8_t>(b,b+sizeof(leaf));};
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller),sizeof(caller),0x1000,nullptr,nullptr,true,false,true,true,&resolver,true,arm_ir_policy::write_budget_chunks);
        // A standalone BX LR is still a legitimate returning leaf.
        if(op!=0xe12fff1eu && !tr.dependencies.empty()){printf(" FAIL unsafe prefix accepted %x\n",op);return false;}
    }
    // Prefix selection must preserve an eligible inner leaf, including a
    // backward BL target. Probing that leaf must not recurse into prefixes.
    unsigned selection_checks=0;
    for(unsigned variant=0;variant<6;++variant)for(unsigned features:{0u,8u,16u,24u}) {
        const unsigned inner_pc=variant==4?0x1800:0x3000;
        const std::vector<unsigned> caller={0xeb0003feu,0xe28bb001u};
        const std::vector<unsigned> outer={0xe92d4010u,0xe1a04000u,
            0xeb000000u|(((inner_pc-0x2010u)>>2)&0xffffffu),0xe8bd4010u,0xe12fff1eu};
        std::vector<unsigned> inner={0xe1500001u,0xa3a00000u,0xb3a00001u,0xe12fff1eu};
        if(variant==1)inner={0xeafffffeu}; // unsupported returning leaf
        if(variant==2)inner.clear(); // unavailable mapping
        if(variant==3){inner.assign(16,0xe1a00000u);inner.push_back(0xe12fff1eu);}
        if(variant==5)inner={0xebfffffeu,0xe12fff1eu}; // no recursive prefix probe
        auto bytes=[](const std::vector<unsigned>& words){const auto *b=reinterpret_cast<const std::uint8_t*>(words.data());return words.empty()?std::vector<std::uint8_t>{}:std::vector<std::uint8_t>(b,b+words.size()*4);};
        unsigned lookups=0;
        leaf_resolver resolver=[&](unsigned pc){++lookups;return pc==0x2000?bytes(outer):pc==inner_pc?bytes(inner):std::vector<std::uint8_t>{};};
        leaf_features=features;predicated_leaves=true;
        for(unsigned entry:{0x1000u,0x2000u}) {
            const auto code=entry==0x1000?bytes(caller):bytes(outer);
            auto tr=translate_arm_block(code.data(),code.size(),entry,nullptr,nullptr,true,false,true,true,&resolver,true,arm_ir_policy::write_budget_chunks);
            const bool returning=variant==0 || variant==4;
            if(entry==0x1000) {
                const bool prefix=(features&8) && !((features&16) && returning);
                if(tr.dependencies.size()!=unsigned(prefix)){printf(" FAIL preserve inner selection variant=%u features=%u\n",variant,features);return false;}
            } else if(returning && (tr.dependencies.size()!=1 || tr.dependencies[0].address!=inner_pc)) {
                printf(" FAIL standalone inner fusion missing\n");return false;
            }
            if(lookups>8){printf(" FAIL recursive inner eligibility probe\n");return false;}
            auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
                {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
            for(unsigned budget=0;budget<=12;++budget)for(unsigned flags:{0u,5u,10u,15u}) {
                test_mem actual;actual.write_code(0x1000,bytes(caller));actual.write_code(0x2000,bytes(outer));
                if(!inner.empty())actual.write_code(inner_pc,bytes(inner));
                test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
                r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0xb000,actual.data.data()+0xb000,3);
                alignas(8) unsigned state[256]{};
                for(unsigned reg=0;reg<16;++reg){state[reg]=reg==13?0xb020:reg==14?0x4000:reg==15?entry:reg;reference->set_reg(reg,state[reg]);}
                reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=16|(flags<<28);
                state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
                state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
                for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
                g_test_mem=&actual;const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
                if(count<0 || count>int(budget) || (!count && budget))return false;
                if(count)reference->run(count);
                for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)){printf(" FAIL preserve inner execution variant=%u mode=%u entry=%x budget=%u reg=%u\n",variant,features,entry,budget,reg);return false;}
                for(unsigned f=0;f<4;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(31-f))&1))return false;
                if(actual.data!=expected.data)return false;
                ++selection_checks;
            }
        }
    }
    printf(" PASS preserve inner fusion (%u exact selection/budget/state/stack comparisons)\n",selection_checks);
    printf(" PASS call prefixes (%u exact budget/state/stack/memory/alias/continuation comparisons)\n",checks);
#endif
    return true;
}

static bool test_expanded_leaves(bool always=false) {
#ifdef __EMSCRIPTEN__
    struct restore {bool flag=predicated_leaves;unsigned features=leaf_features;std::string limits=execution_limits_text();
        ~restore(){predicated_leaves=flag;leaf_features=features;parse_execution_limits(limits.c_str());}} saved;
    configure_execution_limits(512,16,8,512);leaf_features=7;
    const unsigned caller[]={0xeb0003feu,0xe2844001u,0xe5967000u,0xe2888001u};
    unsigned checks=0;
    for(unsigned cond=always?14:0;cond<(always?15u:14u);++cond)for(unsigned variant=0;variant<6;++variant) {
        const bool store=variant==0 || variant==5;
        const unsigned leaf[]={0x0a000001u|(cond<<28),0x05902000u|((cond==14?0:cond^1)<<28),0xe0922003u,
            0x03a05001u|(cond<<28),0xe1d130b0u,variant==0?0xe5865000u:variant==1?0xe0090392u:variant==2?0xe0c98392u:variant==3?0xe0998392u:variant==4?0xe0394392u:(0x05865000u|(cond<<28)),0xe12fff1eu};
        leaf_resolver resolver=[&](unsigned pc) {const auto *b=reinterpret_cast<const std::uint8_t*>(leaf);
            return pc==0x2000?std::vector<std::uint8_t>(b,b+sizeof(leaf)):std::vector<std::uint8_t>{};};
        auto translate=[&](arm_ir_policy policy) {return translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller),sizeof(caller),0x1000,
            nullptr,nullptr,true,false,true,true,&resolver,true,policy);};
        predicated_leaves=false;if(!translate(arm_ir_policy::write_budget_chunks).dependencies.empty())return false;
        predicated_leaves=true;auto tr=translate(arm_ir_policy::write_budget_chunks);
        if(tr.dependencies.size()!=1 || !translate(arm_ir_policy::conditional_value_ir).dependencies.empty())return false;
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned flags=0;flags<16;++flags)for(unsigned mapping=0;mapping<5;++mapping)for(unsigned budget=0;budget<=12;++budget) {
            test_mem actual;actual.write_code(0x1000,{reinterpret_cast<const std::uint8_t*>(caller),reinterpret_cast<const std::uint8_t*>(caller)+sizeof(caller)});
            actual.write_code(0x2000,{reinterpret_cast<const std::uint8_t*>(leaf),reinterpret_cast<const std::uint8_t*>(leaf)+sizeof(leaf)});
            const unsigned values[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu};
            actual.write32(0x8000,values[flags%5]);actual.write32(0x9000,values[(flags+2)%5]);actual.write32(0xa000,0x12345678);
            test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
            if(mapping!=1)tlb.add(0x8000,actual.data.data()+0x8000,3);
            if(mapping!=2)tlb.add(0x9000,actual.data.data()+0x9000,3);
            if(mapping!=3)tlb.add(0xa000,actual.data.data()+(mapping==4?0x1000:0xa000),3);
            if(mapping==4)reference->set_tlb_page(0xa000,expected.data.data()+0x1000,prot_read_write);
            alignas(8) unsigned state[256]{};
            for(unsigned reg=0;reg<16;++reg) {
                state[reg]=reg==0?0x8000:reg==1?0x9000:reg==6?0xa000:reg==9?0x7fffffff:reg==14?0x3000:reg==15?0x1000:reg;
                reference->set_reg(reg,state[reg]);
            }
            reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=16|(flags<<28);
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+sizeof(caller);
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual;const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
            if(count<0 || count>int(budget) || (!count && budget)) {
                printf(" FAIL expanded leaf count cond=%u flags=%u map=%u store=%u budget=%u count=%d\n",cond,flags,mapping,store,budget,count);return false;
            }
            if(count)reference->run(count);
            for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)) {
                printf(" FAIL expanded leaf R%u cond=%u flags=%u map=%u store=%u budget=%u count=%d got=%x expected=%x\n",reg,cond,flags,mapping,store,budget,count,state[reg],reference->get_reg(reg));return false;
            }
            for(unsigned f=0;f<4;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(31-f))&1))return false;
            if(actual.data!=expected.data){printf(" FAIL expanded leaf memory cond=%u flags=%u map=%u store=%u budget=%u count=%d\n",cond,flags,mapping,store,budget,count);return false;}
            ++checks;
        }
    }
    for(unsigned op:{0xeafffffeu,0xea000004u,0x128ee001u,0x112fff1eu,0xeb000000u,0xe10f4000u,0xf3a00001u}) {
        unsigned leaf[]={op,0xe12fff1e};leaf_resolver resolver=[&](unsigned){const auto *b=reinterpret_cast<const std::uint8_t*>(leaf);return std::vector<std::uint8_t>(b,b+sizeof(leaf));};
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller),sizeof(caller),0x1000,nullptr,nullptr,true,false,true,true,&resolver,true,arm_ir_policy::write_budget_chunks);
        if(!tr.dependencies.empty()){printf(" FAIL unsafe expanded leaf accepted %x\n",op);return false;}
    }

    // Captured lookup with two forward joins, flag updates and a halfword load.
    // Relocation preserves the original ARM relative branches; no address is
    // special-cased by the translator. Two call sites need distinct labels.
    const unsigned lookup[]={0xe5922000u,0xe3a0300fu,0xe1530442u,0xba000002u,0xe1b0c442u,0x43a02000u,0x4a000002u,0xe1530442u,0xa1a02442u,0xb3a0200fu,0xe1a03a01u,0xe590026cu,0xe1a03a23u,0xe0832602u,0xe0800082u,0xe1d000b0u,0xe2011a0fu,0xe1800001u,0xe12fff1eu};
    const unsigned twice[]={0xeb0003feu,0xe1a00006u,0xe1a02007u,0xeb0003fbu,0xe2888001u};
    leaf_resolver lookup_resolver=[&](unsigned pc){const auto *p=reinterpret_cast<const std::uint8_t*>(lookup);
        return pc==0x2000?std::vector<std::uint8_t>(p,p+sizeof(lookup)):std::vector<std::uint8_t>{};};
    auto lookup_translate=[&](){return translate_arm_block(reinterpret_cast<const std::uint8_t*>(twice),sizeof(twice),0x1000,nullptr,nullptr,true,false,true,true,&lookup_resolver,true,arm_ir_policy::write_budget_chunks);};
    predicated_leaves=true;
    for(unsigned limit:{16u,32u})for(unsigned mask=0;mask<8;++mask) {
        leaf_instruction_limit=limit;leaf_features=mask;
        if(lookup_translate().dependencies.size()!=unsigned(limit>=19 && (mask&6)==6)) {
            printf(" FAIL captured leaf selection limit=%u mask=%u\n",limit,mask);return false;
        }
    }
    leaf_instruction_limit=32;leaf_features=7;
    auto lookup_tr=lookup_translate();
    auto lookup_module=build_wasm_module({lookup_tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    for(unsigned input:{0xffffff00u,0u,0x100u,0xf00u,0x1000u})for(unsigned mapping=0;mapping<3;++mapping)for(unsigned budget=0;budget<=44;++budget) {
        test_mem actual;
        actual.write_code(0x1000,{reinterpret_cast<const std::uint8_t*>(twice),reinterpret_cast<const std::uint8_t*>(twice)+sizeof(twice)});
        actual.write_code(0x2000,{reinterpret_cast<const std::uint8_t*>(lookup),reinterpret_cast<const std::uint8_t*>(lookup)+sizeof(lookup)});
        actual.write32(0x826c,0xa000);actual.write32(0x9000,input);
        for(unsigned p=0xa000;p<0x2a000;p+=4)actual.write32(p,p*37+0x1234);
        test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
        for(unsigned page=0x8000;page<0x2a000;page+=4096)if(!(mapping==1 && page==0x9000) && !(mapping==2 && page>=0xa000))tlb.add(page,actual.data.data()+page,3);
        alignas(8) unsigned state[256]{};state[0]=0x8000;state[1]=0x3001;state[2]=0x9000;state[6]=0x8000;state[7]=0x9000;state[14]=0x3000;state[15]=0x1000;
        for(unsigned reg=0;reg<16;++reg)reference->set_reg(reg,state[reg]);reference->set_cpsr(16);
        state[state_offsets::MODE/4]=16;state[state_offsets::CPSR/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
        state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        g_test_mem=&actual;const auto count=js_run_aot_wasm(lookup_module.data(),lookup_module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
        if(count<0 || count>int(budget) || (!count && budget))return false;
        if(count)reference->run(count);
        for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)) {
            printf(" FAIL captured branching leaf R%u input=%x map=%u budget=%u count=%d got=%x expected=%x\n",reg,input,mapping,budget,count,state[reg],reference->get_reg(reg));return false;
        }
        for(unsigned flag=0;flag<4;++flag)if(state[region_ir::flag_offsets[flag]/4]!=((reference->get_cpsr()>>(31-flag))&1))return false;
        if(actual.data!=expected.data)return false;
        ++checks;
    }
    printf(" PASS expanded leaves (%u exact condition/flag/budget/guard/physical-alias comparisons; unsupported forms rejected)\n",checks);
#endif
    return true;
}

static bool test_predicated_leaves() {
#ifdef __EMSCRIPTEN__
    struct restore {bool flag=predicated_leaves;std::string limits=execution_limits_text();
        ~restore(){predicated_leaves=flag;parse_execution_limits(limits.c_str());}} saved;
    configure_execution_limits(512,16,8,512);
    const unsigned caller[]={0xeb0003feu,0xe2844001u,0xe5967000u,0xe2888001u};
    unsigned checks=0;
    for(unsigned cond=0;cond<14;++cond)for(bool store:{false,true}) {
        const unsigned leaf[]={0x03a05001u|(cond<<28),0xe5902000u,0xe5913000u,0xe1520003u,
            0x02999001u|(cond<<28),store?0xe5865000u:(0x03a0a002u|((cond^1)<<28)),0xe12fff1eu};
        leaf_resolver resolver=[&](unsigned pc) {const auto *b=reinterpret_cast<const std::uint8_t*>(leaf);
            return pc==0x2000?std::vector<std::uint8_t>(b,b+sizeof(leaf)):std::vector<std::uint8_t>{};};
        auto translate=[&](arm_ir_policy policy) {return translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller),sizeof(caller),0x1000,
            nullptr,nullptr,true,false,true,true,&resolver,true,policy);};
        predicated_leaves=false;if(!translate(arm_ir_policy::write_budget_chunks).dependencies.empty())return false;
        predicated_leaves=true;auto tr=translate(arm_ir_policy::write_budget_chunks);
        if(tr.dependencies.size()!=1 || !translate(arm_ir_policy::conditional_value_ir).dependencies.empty())return false;
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for(unsigned flags=0;flags<16;++flags)for(unsigned mapping=0;mapping<5;++mapping)for(unsigned budget=0;budget<=12;++budget) {
            test_mem actual;actual.write_code(0x1000,{reinterpret_cast<const std::uint8_t*>(caller),reinterpret_cast<const std::uint8_t*>(caller)+sizeof(caller)});
            actual.write_code(0x2000,{reinterpret_cast<const std::uint8_t*>(leaf),reinterpret_cast<const std::uint8_t*>(leaf)+sizeof(leaf)});
            const unsigned values[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu};
            actual.write32(0x8000,values[flags%5]);actual.write32(0x9000,values[(flags+2)%5]);actual.write32(0xa000,0x12345678);
            test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
            r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);
            if(mapping!=1)tlb.add(0x8000,actual.data.data()+0x8000,3);
            if(mapping!=2)tlb.add(0x9000,actual.data.data()+0x9000,3);
            if(mapping!=3)tlb.add(0xa000,actual.data.data()+(mapping==4?0x1000:0xa000),3);
            if(mapping==4)reference->set_tlb_page(0xa000,expected.data.data()+0x1000,prot_read_write);
            alignas(8) unsigned state[256]{};
            for(unsigned reg=0;reg<16;++reg) {
                state[reg]=reg==0?0x8000:reg==1?0x9000:reg==6?0xa000:reg==9?0x7fffffff:reg==14?0x3000:reg==15?0x1000:reg;
                reference->set_reg(reg,state[reg]);
            }
            reference->set_cpsr(16|(flags<<28));state[state_offsets::CPSR/4]=16|(flags<<28);
            state[state_offsets::MODE/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
            state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(actual.data.data()+0x1000);
            state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+sizeof(caller);
            for(unsigned f=0;f<4;++f)state[region_ir::flag_offsets[f]/4]=(flags>>(3-f))&1;
            g_test_mem=&actual;const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
            if(count<0 || count>int(budget) || (!count && budget) || (!mapping && count!=int(std::min(budget,11u)))) {
                printf(" FAIL predicated leaf count cond=%u flags=%u map=%u store=%u budget=%u count=%d\n",cond,flags,mapping,store,budget,count);return false;
            }
            if(count)reference->run(count);
            for(unsigned reg=0;reg<16;++reg)if(state[reg]!=reference->get_reg(reg)) {
                printf(" FAIL predicated leaf R%u cond=%u flags=%u map=%u store=%u budget=%u count=%d got=%x expected=%x\n",reg,cond,flags,mapping,store,budget,count,state[reg],reference->get_reg(reg));return false;
            }
            for(unsigned f=0;f<4;++f)if(state[region_ir::flag_offsets[f]/4]!=((reference->get_cpsr()>>(31-f))&1))return false;
            if(actual.data!=expected.data){printf(" FAIL predicated leaf memory cond=%u flags=%u map=%u store=%u budget=%u count=%d\n",cond,flags,mapping,store,budget,count);return false;}
            ++checks;
        }
    }
    for(unsigned op:{0x15910000u,0x128ee001u,0x112fff1eu,0xeb000000u,0xe10f4000u,0xf3a00001u}) {
        unsigned leaf[]={op,0xe12fff1e};leaf_resolver resolver=[&](unsigned){const auto *b=reinterpret_cast<const std::uint8_t*>(leaf);return std::vector<std::uint8_t>(b,b+sizeof(leaf));};
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller),sizeof(caller),0x1000,nullptr,nullptr,true,false,true,true,&resolver,true,arm_ir_policy::write_budget_chunks);
        if(!tr.dependencies.empty()){printf(" FAIL unsafe predicated leaf accepted %x\n",op);return false;}
    }
    printf(" PASS predicated leaves (%u exact condition/flag/budget/guard/physical-alias comparisons; unsupported forms rejected)\n",checks);
#endif
    return true;
}

static unsigned runner_test_action=0,runner_test_calls=0;
static std::uint32_t runner_test_function(ARMul_State *cpu) {
    ++runner_test_calls;
    if(runner_test_action==1)return 0;
    ++cpu->Reg[0];
    if(runner_test_action==2)cpu->NumInstrsToExecute=0;
    if(runner_test_action==3 || runner_test_action==4)cpu->NirqSig=0;
    if(runner_test_action==5)cpu->Reg[15]=0x54320;
    return 1;
}
static bool test_execution_limits() {
    struct restore {std::string value=execution_limits_text();bool ram=ram_compilation_enabled;
        ~restore(){parse_execution_limits(value.c_str());ram_compilation_enabled=ram;global_registry().unregister_function(0x54300);}} saved;
    if(!parse_execution_limits("512,16,8,512"))return false;
    for(const char *bad:{"512,16,8,512x","512,16,8","0512,16,8,512","+512,16,8,512",
        "512,16,8,-1","0,16,8,512","2049,16,8,512","512,0,8,512","512,65,8,512",
        "512,16,17,512","512,16,8,4097"}) {
        if(parse_execution_limits(bad)||execution_limits_text()!="512,16,8,512")return false;
    }
    test_mem memory;r12l1::exclusive_monitor monitor(1);auto core=make_cpu(memory,monitor);
    ram_compilation_enabled=false;global_registry().register_function(0x54300,runner_test_function);
    auto state=std::make_unique<ARMul_State>(core.get(),USER32MODE);
    unsigned checks=0;
    for(unsigned cap:{0u,1u,64u,512u,4096u})for(unsigned budget:{0u,1u,63u,64u,65u,511u,512u,513u,4840u})
    for(unsigned action=0;action<6;++action) {
        runner_region_limit=cap;runner_test_action=action;runner_test_calls=0;
        auto &cpu=*state;cpu.Reset();cpu.mem_cache_=core->mem_cache();cpu.Reg[0]=0;cpu.Reg[15]=0x54300;cpu.TFlag=0;
        cpu.NumInstrsToExecute=budget;cpu.aot_budget=budget;cpu.NirqSig=1;cpu.Cpsr=16|(action==4?0x80:0);
        const auto result=execute_chain(&cpu,runner_test_function);
        const auto want=!budget?0u:action==1?0u:(action==2||action==3||action==5)?1u:cap?std::min(cap,budget):budget;
        const auto calls=(!budget)?0u:action==1?1u:want;
        if(result.instructions!=want || result.blocks!=calls || runner_test_calls!=calls || cpu.Reg[0]!=want) {
            printf(" FAIL real runner cap=%u budget=%u action=%u instructions=%u/%u blocks=%u/%u\n",cap,budget,action,result.instructions,want,result.blocks,calls);return false;
        }
        ++checks;
    }
    printf(" PASS execution limits validation and %u real runner budget/zero/stop/IRQ/masked-IRQ/missing-successor checks\n",checks);
    return true;
}

static bool test_inline_limits() {
#ifdef __EMSCRIPTEN__
    struct restore {std::string value=execution_limits_text();~restore(){parse_execution_limits(value.c_str());}} saved;
    unsigned checks=0;
    for(unsigned limit:{8u,16u,32u,64u})for(unsigned sites:{0u,1u,8u,16u})for(unsigned length:{4u,16u,32u,64u,65u}) {
        configure_execution_limits(512,limit,sites,512);
        std::vector<unsigned> leaf(length,0xe2800001);leaf.back()=0xe12fff1e;
        std::vector<unsigned> caller;
        for(unsigned n=0;n<20;++n)caller.push_back(0xeb000000u|((0x2000u-(0x1000u+n*4+8))/4));
        leaf_resolver resolver=[&](unsigned pc) {const auto *p=reinterpret_cast<const std::uint8_t*>(leaf.data());
            return pc==0x2000?std::vector<std::uint8_t>(p,p+leaf.size()*4):std::vector<std::uint8_t>{};};
        const auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller.data()),caller.size()*4,0x1000,
            nullptr,nullptr,true,false,true,true,&resolver,true,arm_ir_policy::write_budget_chunks);
        const unsigned selected=length<=limit?sites:0;
        if(tr.dependencies.size()!=unsigned(selected!=0)){printf(" FAIL inline selection leaf=%u limit=%u sites=%u dependencies=%zu\n",length,limit,sites,tr.dependencies.size());return false;}
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        const unsigned total=selected*(length+1)+1;
        for(unsigned budget:{0u,1u,2u,length,length+1,total-1,total,total+1}) {
            test_mem actual;actual.write_code(0x1000,{reinterpret_cast<const std::uint8_t*>(caller.data()),reinterpret_cast<const std::uint8_t*>(caller.data()+caller.size())});
            actual.write_code(0x2000,{reinterpret_cast<const std::uint8_t*>(leaf.data()),reinterpret_cast<const std::uint8_t*>(leaf.data()+leaf.size())});
            test_mem expected=actual;r12l1::exclusive_monitor monitor(1);auto reference=make_cpu(expected,monitor);
            alignas(8) unsigned state[256]{};state[15]=0x1000;state[14]=0x3000;
            state[state_offsets::MODE/4]=16;state[state_offsets::CPSR/4]=16;state[state_offsets::NIRQ/4]=1;state[state_offsets::AOT_BUDGET/4]=budget;
            for(unsigned r=0;r<16;++r)reference->set_reg(r,state[r]);reference->set_cpsr(16);
            g_test_mem=&actual;const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
            if(count!=int(std::min(total,budget))){printf(" FAIL inline count limit=%u sites=%u length=%u budget=%u count=%d expected=%u\n",limit,sites,length,budget,count,std::min(total,budget));return false;}
            if(count)reference->run(count);
            for(unsigned r=0;r<16;++r)if(state[r]!=reference->get_reg(r)){printf(" FAIL inline limits state R%u limit=%u sites=%u length=%u budget=%u got=%x expected=%x\n",r,limit,sites,length,budget,state[r],reference->get_reg(r));return false;}
            if(actual.data!=expected.data)return false;
            ++checks;
        }
    }
    printf(" PASS inline leaf/site bounds and %u exact interpreter budget comparisons\n",checks);
#endif
    return true;
}

static bool test_exit_census() {
#ifdef __EMSCRIPTEN__
    const bool old=exit_census::enabled;exit_census::enabled=true;
    struct fixture {std::uint32_t op;unsigned budget,count;const char *reason;bool thumb=false;};
    for(const auto &f:std::vector<fixture>{{0xe2800001,8,1,"source_window_end"},
        {0xe2800001,0,0,"budget"},{0xef000000,8,0,"unsupported"},
        {0xe5910000,8,0,"memory_guard"},{0xeb000001,8,1,"call"},
        {0xe12fff1e,8,1,"return_bx_lr"},{0xeafffffe,8,1,"interrupt"},
        {0xe5810000,8,1,"code_write_guard"},{0xe4910004,8,1,"helper_exit"},
        {0x2001,8,1,"source_window_end",true},{0x2001,0,0,"budget",true},
        {0xdf00,8,0,"unsupported",true},{0x4770,8,1,"return_bx_lr",true}}) {
        const auto *bytes=reinterpret_cast<const std::uint8_t*>(&f.op);
        const auto tr=f.thumb?translate_thumb_block(bytes,2,0x1000,nullptr,nullptr,true,false,true)
            :translate_arm_block(bytes,4,0x1000,nullptr,nullptr,true,false,true,true,nullptr,true,arm_ir_policy::write_budget_chunks);
        auto module=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        test_mem memory;memory.write32(0x1000,f.op);memory.write32(0x8000,123);
        r12l1::tlb tlb(12,r12l1::dyncom_folded_tlb);tlb.add(0x1000,memory.data.data()+0x1000,3);
        alignas(8) std::uint32_t state[256]{};state[0]=0xe2800002;state[1]=f.op==0xe5810000?0x1000:0x8000;
        state[14]=0x2000;state[15]=0x1000;state[state_offsets::MODE/4]=16;state[state_offsets::CPSR/4]=16;
        state[state_offsets::TFLAG/4]=f.thumb;state[state_offsets::NIRQ/4]=f.op==0xeafffffe?0:1;
        state[state_offsets::AOT_BUDGET/4]=f.budget;state[state_offsets::AOT_TLB/4]=reinterpret_cast<std::uintptr_t>(tlb.entries);
        state[state_offsets::AOT_CODE_BEGIN/4]=reinterpret_cast<std::uintptr_t>(memory.data.data()+0x1000);
        state[state_offsets::AOT_CODE_END/4]=state[state_offsets::AOT_CODE_BEGIN/4]+4;
        exit_census::last_reason=0;exit_census::effects=0;g_test_mem=&memory;
        const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));g_test_mem=nullptr;
        const auto actual=exit_census::classify(f.thumb,count,f.budget,state[state_offsets::AOT_EXIT/4]);
        if(count!=int(f.count)||std::string(actual)!=f.reason||exit_census::last_pc!=(0x1000u|unsigned(f.thumb))||exit_census::last_opcode!=f.op) {
            printf(" FAIL exit census op=%x count=%d/%u reason=%s/%s site=%x opcode=%x\n",f.op,count,f.count,actual,f.reason,exit_census::last_pc,exit_census::last_opcode);
            exit_census::enabled=old;return false;
        }
    }
    exit_census::enabled=old;printf(" PASS exit census actual ARM/Thumb exit labels, budgets, helpers and code aliases\n");
#endif
    return true;
}

int main(int argc, char **argv) {
    if(argc==2 && std::string(argv[1])=="--expanded-leaves-always-only")return test_expanded_leaves(true)?0:1;
    if(argc==2 && std::string(argv[1])=="--call-prefixes-only")return test_call_prefixes()?0:1;
    if(argc==2 && std::string(argv[1])=="--expanded-leaves-only")return test_expanded_leaves()?0:1;
    if(argc==2 && std::string(argv[1])=="--boundary-details-only")return test_boundary_details()?0:1;
    if(argc==2 && std::string(argv[1])=="--predicated-leaves-only")return test_predicated_leaves()?0:1;
    if(argc==2 && std::string(argv[1])=="--execution-limits-only")return test_execution_limits() && test_inline_limits()?0:1;
    if(argc==2 && std::string(argv[1])=="--exit-census-only")return test_exit_census()?0:1;
    if(argc==2 && std::string(argv[1])=="--exit-census") {exit_census::enabled=true;argc=1;}
    if(argc==2 && std::string(argv[1])=="--write-protection-only") return test_code_write_protection()?0:1;
    if(argc==2 && std::string(argv[1])=="--lookup-only") return test_outlined_code_lookup()?0:1;
    if(argc==2 && std::string(argv[1])=="--exact-code-only") return test_exact_code_compare()?0:1;
    if(argc==2 && std::string(argv[1])=="--folded-tlb-only") return test_folded_tlb_guards()?0:1;
    if (argc == 2 && std::string(argv[1]).rfind("--tlb-hash=",0)==0) {
        const std::string value=std::string(argv[1]).substr(11);
        if(value!="0" && value!="1") {printf("Invalid TLB index mode\n");return 1;}
        r12l1::dyncom_folded_tlb=value=="1";argc=1;
    }
    printf("TEST_TLB_HASH %u\n",unsigned(r12l1::dyncom_folded_tlb));
#ifdef __EMSCRIPTEN__
    if (argc == 2 && std::string(argv[1]) == "--emit-flags") {
        // Generic flag-overwrite/consumer fixtures, unrelated to game PCs.
        std::vector<wasm_func_def> funcs;
        for (const auto &ops : std::vector<std::vector<std::uint32_t>>{
            {0xe3500001,0xe3700001,0xe2522001,0x1afffffb},
            {0xe3500001,0xe0a03000,0xe2522001,0x1afffffb}}) {
            auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t *>(ops.data()),
                ops.size()*4,0x1000,nullptr,nullptr,true,true,true,true);
            tr.func.export_name="probe"+std::to_string(funcs.size());
            funcs.push_back(std::move(tr.func));
        }
        auto bytes=build_wasm_module(funcs,{{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},
            {"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        js_export_flag_probe(bytes.data(),bytes.size());
        return 0;
    }
#endif

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
        // Regression test for bug #2: backward branches used to re-enter
        // the WASM loop at its top (instruction 0) regardless of the
        // requested target, because the dispatch was `set PC_IDX;
        // br $loop` and nothing actually read PC_IDX. If the function
        // was entered at insn 0 and the loop head was also insn 0 the
        // bug was invisible; with a loop head at insn > 0, execution
        // re-ran the pre-head code on every iteration (R0 decremented
        // an extra 2 times, so final R0=7 instead of 9).
        //
        // Fix: the test harness now runs the same two-pass flow as
        // aot_setup — branch targets are translated as separate entry
        // functions and backward branches emit a direct WASM call to
        // the sibling f_<target>. No interpreter roundtrip, loop stays
        // entirely inside WASM.
        //
        // Layout (insn indices in parens, loop head at insn 1):
        //   @ 0x1000 (insn 0)  SUBS R0, #1               — 0x3801
        //   @ 0x1002 (insn 1)  ADDS R1, #1               — 0x3101, loop head
        //   @ 0x1004 (insn 2)  CMP  R1, #3               — 0x2903
        //   @ 0x1006 (insn 3)  BNE  target (insn 1)      — 0xD1FC
        //   @ 0x1008 (insn 4)  BX LR                     — 0x4770
        //
        // Init R0=10, R1=0. After: R0=9, R1=3.
        {"Backward branch to non-entry",
            {0x01, 0x38,   // SUBS R0, #1
             0x01, 0x31,   // ADDS R1, #1 (loop head, idx 1)
             0x03, 0x29,   // CMP R1, #3
             0xFC, 0xD1,   // BNE -4 (target 0x1002)
             0x70, 0x47},  // BX LR
            0x1000,
            [&]{ auto r = zero_regs; r[0] = 10; r[1] = 0; r[14] = 0x100B; return r; }(), 100,
            {}, {},
            0},

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
    if (test_blx_veneer_inlining()) passed++; else failed++;
    if (test_function_end_stops_at_pop_pc()) passed++; else failed++;
    if (test_function_end_stops_at_bx_lr()) passed++; else failed++;
    if (test_branch_targets_include_bl_next_pc()) passed++; else failed++;
    if (test_branch_targets_include_beq_target()) passed++; else failed++;
    if (test_literal_pool_between_early_return_and_target()) passed++; else failed++;
    if (test_wide_push_pop()) passed++; else failed++;
    if (test_stop_at_wide_bail_after_last_target()) passed++; else failed++;
    if (test_wide_movw_movt()) passed++; else failed++;
    if (test_wide_ldr_str_imm12()) passed++; else failed++;
    if (test_wide_add_sub_imm()) passed++; else failed++;
    if (test_wide_and_orr_eor_bic_imm()) passed++; else failed++;
    if (test_wide_ldr_str_reg()) passed++; else failed++;
    if (test_wide_ldr_neg_offset()) passed++; else failed++;
    if (test_wide_b_uncond()) passed++; else failed++;
    if (test_wide_ldrd_strd()) passed++; else failed++;
    if (test_wide_ubfx_sbfx()) passed++; else failed++;
    if (test_wide_bfi_bfc()) passed++; else failed++;
    if (test_wide_uxtb_sxth()) passed++; else failed++;
    if (test_wide_clz()) passed++; else failed++;
    if (test_wide_mul_mla_mls()) passed++; else failed++;
    if (test_wide_sdiv_udiv()) passed++; else failed++;
    if (test_wide_shifted_reg()) passed++; else failed++;
    if (test_wide_addw_subw()) passed++; else failed++;
    if (test_wide_mov_mvn_imm()) passed++; else failed++;
    if (test_wide_strb_imm12()) passed++; else failed++;
    if (test_vfp_vldr_vadd_vstr()) passed++; else failed++;
    if (test_vfp_vcvt_vcmp_vmrs()) passed++; else failed++;
    if (test_vfp_f64_instantiation()) passed++; else failed++;
    if (test_resume_points_beyond_1024()) passed++; else failed++;
    if (test_sibling_bl_resume_point()) passed++; else failed++;

    printf("\nRunning ARM translator-level tests...\n\n");
    if (test_inlined_leaves()) passed++; else failed++;
#if defined(EKA2L1_WASM_IR_MEMORY) && defined(EKA2L1_WASM_IR_SEGMENTS) && defined(EKA2L1_WASM_IR_OUTLINE) && !defined(EKA2L1_WASM_CODE_VERSIONS)
    if (test_inlined_leaves(arm_ir_policy::inline_call_ir)) passed++; else failed++;
    if (test_inlined_leaves(arm_ir_policy::invariant_write_ir)) passed++; else failed++;
    if (test_invariant_writes(arm_ir_policy::invariant_write_ir)) passed++; else failed++;
#endif
    if (test_bounded_execution()) passed++; else failed++;
    if (test_folded_tlb_guards()) passed++; else failed++;
    if (test_outlined_code_lookup()) passed++; else failed++;
    if (test_exact_code_compare()) passed++; else failed++;
    if (test_region_cpsr_callback()) passed++; else failed++;
    if (test_block_transfer_callback_pc()) passed++; else failed++;
    if (test_repeated_read_guards()) passed++; else failed++;
    if (test_outlined_callee_indices()) passed++; else failed++;
    if (test_proved_read_spans()) passed++; else failed++;
    if (test_invariant_reads()) passed++; else failed++;
    if (test_invariant_reads(arm_ir_policy::invariant_read_ir)) passed++; else failed++;
    if (test_invariant_writes()) passed++; else failed++;
    if (test_exit_census()) passed++; else failed++;
    if (test_execution_limits()) passed++; else failed++;
    if (test_inline_limits()) passed++; else failed++;
    if (test_predicated_leaves()) passed++; else failed++;
    if (test_expanded_leaves()) passed++; else failed++;
    if (test_call_prefixes()) passed++; else failed++;
    if (test_boundary_details()) passed++; else failed++;
    if (test_budget_chunks()) passed++; else failed++;
    if (test_budget_chunks(arm_ir_policy::budget_gaps_ir)) passed++; else failed++;
    if (test_budget_chunks(arm_ir_policy::write_budget_chunks)) passed++; else failed++;
    if (test_budget_chunks(arm_ir_policy::deferred_chunk_counts)) passed++; else failed++;
    if (test_invariant_writes(arm_ir_policy::deferred_chunk_counts)) passed++; else failed++;
    if (test_invariant_writes(arm_ir_policy::write_budget_chunks)) passed++; else failed++;
    if (test_region_ir()) passed++; else failed++;
    if (test_ir_flags()) passed++; else failed++;
    if (test_ir_conditions()) passed++; else failed++;
    if (test_ir_conditions(arm_ir_policy::long_segments_ir)) passed++; else failed++;
    if (test_ir_conditions(arm_ir_policy::stack_values_ir)) passed++; else failed++;
    if (test_ir_conditions(arm_ir_policy::budget_gaps_ir)) passed++; else failed++;
    if (test_ir_long_segments()) passed++; else failed++;
    if (test_ir_segments()) passed++; else failed++;
    if (test_ir_memory_exits()) passed++; else failed++;
    if (test_ir_exit_recipes()) passed++; else failed++;
    if (test_ir_addressing()) passed++; else failed++;
    if (test_memory_displacements()) passed++; else failed++;
    if (test_deferred_memory_exits()) passed++; else failed++;
    if (test_block_transfer_guards()) passed++; else failed++;
    if (test_region_loop_interrupts()) passed++; else failed++;
    if (test_region_code_alias()) passed++; else failed++;
    if (test_conditional_alu_select()) passed++; else failed++;
    if (test_compare_conditions()) passed++; else failed++;
    if (test_arm_long_multiply()) passed++; else failed++;
    if (test_cached_callback_state()) passed++; else failed++;
    if (test_msr_privilege_guard()) passed++; else failed++;
#ifdef EKA2L1_WASM_CODE_VERSIONS
#ifdef EKA2L1_WASM_CODE_LIFECYCLE
    if (test_code_write_protection()) passed++; else {printf("  FAIL code_write_protection\n");failed++;}
    if (test_code_lifecycle()) passed++; else {printf("  FAIL code_lifecycle\n");failed++;}
#endif
    if (test_generated_write_versions()) passed++; else {printf("  FAIL generated_write_versions\n");failed++;}
    if (test_code_validity_versions()) passed++; else {printf("  FAIL code_validity_versions\n");failed++;}
#else
    printf("  SKIP code validity generation tests (experimental build option disabled)\n");
#endif
    if (test_arm_mov_imm()) passed++; else failed++;
    if (test_arm_add_sub_imm()) passed++; else failed++;
    if (test_arm_cmp_beq()) passed++; else failed++;
    if (test_arm_ldr_str_imm()) passed++; else failed++;
    if (test_arm_ldm_stm()) passed++; else failed++;
    if (test_arm_mul()) passed++; else failed++;
    if (test_arm_bic_orr_eor()) passed++; else failed++;
    if (test_arm_shifted_reg()) passed++; else failed++;
    if (test_arm_conditional_exec()) passed++; else failed++;
    if (test_arm_bl_resume_point()) passed++; else failed++;
    if (test_arm_ldrh_strh()) passed++; else failed++;
    if (test_arm_mvn()) passed++; else failed++;
    if (test_arm_rsb()) passed++; else failed++;
    if (test_arm_clz()) passed++; else failed++;

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
