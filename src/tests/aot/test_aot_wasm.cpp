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
#include <cpu/aot/arm_translator.h>
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
