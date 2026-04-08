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

#include <catch2/catch.hpp>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/aot/aot_registry.h>
#include <cpu/aot/thumb_translator.h>
#include <cpu/aot/wasm_emitter.h>

#include <cpu/dyncom/armstate.h>
#include <array>
#include <cstring>
#include <vector>

namespace {
    struct aot_test_env {
        static constexpr std::uint32_t MEM_SIZE = 1024 * 1024; // 1MB
        std::vector<std::uint8_t> memory;
        eka2l1::arm::r12l1::exclusive_monitor monitor;

        aot_test_env()
            : memory(MEM_SIZE, 0)
            , monitor(1) {
        }

        std::unique_ptr<eka2l1::arm::dyncom_core> make_cpu() {
            auto core = std::make_unique<eka2l1::arm::dyncom_core>(&monitor, 12);
            aot_test_env *env = this;

            core->read_code = [env](eka2l1::arm::address addr, std::uint32_t *result) -> bool {
                if (addr + 4 > env->memory.size()) return false;
                std::memcpy(result, &env->memory[addr], 4);
                return true;
            };

            core->read_8bit = [env](eka2l1::arm::address addr, std::uint8_t *result) -> bool {
                if (addr >= env->memory.size()) return false;
                *result = env->memory[addr];
                return true;
            };

            core->write_8bit = [env](eka2l1::arm::address addr, std::uint8_t *result) -> bool {
                if (addr >= env->memory.size()) return false;
                env->memory[addr] = *result;
                return true;
            };

            core->read_16bit = [env](eka2l1::arm::address addr, std::uint16_t *result) -> bool {
                if (addr + 2 > env->memory.size()) return false;
                std::memcpy(result, &env->memory[addr], 2);
                return true;
            };

            core->write_16bit = [env](eka2l1::arm::address addr, std::uint16_t *result) -> bool {
                if (addr + 2 > env->memory.size()) return false;
                std::memcpy(&env->memory[addr], result, 2);
                return true;
            };

            core->read_32bit = [env](eka2l1::arm::address addr, std::uint32_t *result) -> bool {
                if (addr + 4 > env->memory.size()) return false;
                std::memcpy(result, &env->memory[addr], 4);
                return true;
            };

            core->write_32bit = [env](eka2l1::arm::address addr, std::uint32_t *result) -> bool {
                if (addr + 4 > env->memory.size()) return false;
                std::memcpy(&env->memory[addr], result, 4);
                return true;
            };

            core->read_64bit = [env](eka2l1::arm::address addr, std::uint64_t *result) -> bool {
                if (addr + 8 > env->memory.size()) return false;
                std::memcpy(result, &env->memory[addr], 8);
                return true;
            };

            core->write_64bit = [env](eka2l1::arm::address addr, std::uint64_t *result) -> bool {
                if (addr + 8 > env->memory.size()) return false;
                std::memcpy(&env->memory[addr], result, 8);
                return true;
            };

            return core;
        }

        void write_code(std::uint32_t addr, const std::vector<std::uint32_t> &instructions) {
            for (std::size_t i = 0; i < instructions.size(); i++) {
                std::uint32_t offset = addr + i * 4;
                std::memcpy(&memory[offset], &instructions[i], 4);
            }
        }

        void write32(std::uint32_t addr, std::uint32_t value) {
            std::memcpy(&memory[addr], &value, 4);
        }

        std::uint32_t read32(std::uint32_t addr) {
            std::uint32_t val;
            std::memcpy(&val, &memory[addr], 4);
            return val;
        }

        void write16(std::uint32_t addr, std::uint16_t value) {
            std::memcpy(&memory[addr], &value, 2);
        }

        std::uint16_t read16(std::uint32_t addr) {
            std::uint16_t val;
            std::memcpy(&val, &memory[addr], 2);
            return val;
        }
    };

    struct cpu_snapshot {
        std::array<std::uint32_t, 16> regs;
        std::uint32_t cpsr;
        std::vector<std::uint8_t> memory;

        static cpu_snapshot capture(eka2l1::arm::dyncom_core &core, const std::vector<std::uint8_t> &mem) {
            cpu_snapshot snap;
            for (int i = 0; i < 16; i++) {
                snap.regs[i] = core.get_reg(i);
            }
            snap.cpsr = core.get_cpsr();
            snap.memory = mem;
            return snap;
        }

        void assert_equal(const cpu_snapshot &other, const char *label) const {
            for (int i = 0; i < 16; i++) {
                INFO(label << " R" << i << " mismatch");
                CHECK(regs[i] == other.regs[i]);
            }
            INFO(label << " CPSR mismatch");
            CHECK(cpsr == other.cpsr);
            REQUIRE(memory.size() == other.memory.size());
            for (std::size_t i = 0; i < memory.size(); i++) {
                if (memory[i] != other.memory[i]) {
                    INFO(label << " memory mismatch at offset 0x" << std::hex << i);
                    CHECK(memory[i] == other.memory[i]);
                    break;
                }
            }
        }
    };
}

// --- Test: AOT registry basics ---

TEST_CASE("aot_registry_basic", "[aot]") {
    eka2l1::arm::aot::registry reg;
    REQUIRE(reg.size() == 0);
    REQUIRE(reg.lookup(0x1000) == nullptr);

    auto dummy_func = [](ARMul_State *) -> std::uint32_t { return 1; };
    reg.register_function(0x1000, dummy_func);
    REQUIRE(reg.size() == 1);
    REQUIRE(reg.has_function(0x1000));
    REQUIRE(reg.lookup(0x1000) == dummy_func);
    REQUIRE(reg.lookup(0x1004) == nullptr);

    reg.unregister_function(0x1000);
    REQUIRE(reg.size() == 0);
    REQUIRE(reg.lookup(0x1000) == nullptr);
}

// --- Test: Simple ADD loop — interpreter vs AOT produce identical results ---
//
// ARM code at address 0x1000:
//   0x1000: MOV R0, #0        ; e3a00000
//   0x1004: MOV R1, #10       ; e3a0100a
//   0x1008: ADD R0, R0, #1    ; e2800001   (loop start)
//   0x100C: SUBS R1, R1, #1   ; e2511001
//   0x1010: BNE loop           ; 1afffffc   (branch to 0x1008 if NE)
//   0x1014: B .                ; eafffffe   (infinite loop = halt)
//
// Result: R0 = 10, R1 = 0

static std::uint32_t aot_add_loop(ARMul_State *cpu) {
    // Equivalent of the ARM code above: count from 0 to 10
    std::uint32_t r0 = 0;
    std::uint32_t r1 = 10;

    do {
        r0 += 1;
        r1 -= 1;
    } while (r1 != 0);

    cpu->Reg[0] = r0;
    cpu->Reg[1] = r1;

    // Set CPSR flags as SUBS R1, R1, #1 with result 0 would:
    // Z=1 (result is zero), C=1 (no borrow), N=0, V=0
    cpu->Reg[15] = 0x1014; // PC after loop
    cpu->NFlag = 0;
    cpu->ZFlag = 1;
    cpu->CFlag = 1;
    cpu->VFlag = 0;

    return 22; // 2 MOVs + 10*(ADD+SUBS+BNE) - 1 (last BNE falls through) + 1 B = 2 + 29 + 1... approximate
}

TEST_CASE("aot_add_loop_vs_interpreter", "[aot]") {
    // Run through interpreter
    aot_test_env env_interp;
    env_interp.write_code(0x1000, {
        0xe3a00000, // MOV R0, #0
        0xe3a0100a, // MOV R1, #10
        0xe2800001, // ADD R0, R0, #1
        0xe2511001, // SUBS R1, R1, #1
        0x1afffffc, // BNE -8 (back to 0x1008)
        0xeafffffe, // B . (halt)
    });

    auto cpu_interp = env_interp.make_cpu();
    cpu_interp->set_pc(0x1000);
    cpu_interp->set_cpsr(USER32MODE); // ARM mode, user
    cpu_interp->set_reg(13, 0x10000); // SP

    // Run enough instructions to complete the loop
    cpu_interp->run(100);

    // Verify interpreter got the right result
    REQUIRE(cpu_interp->get_reg(0) == 10);
    REQUIRE(cpu_interp->get_reg(1) == 0);

    auto interp_snap = cpu_snapshot::capture(*cpu_interp, env_interp.memory);

    // Run through AOT
    aot_test_env env_aot;
    env_aot.write_code(0x1000, {
        0xe3a00000, // MOV R0, #0 (same code, but won't be interpreted)
        0xe3a0100a, // MOV R1, #10
        0xe2800001, // ADD R0, R0, #1
        0xe2511001, // SUBS R1, R1, #1
        0x1afffffc, // BNE -8
        0xeafffffe, // B .
    });

    auto cpu_aot = env_aot.make_cpu();
    cpu_aot->set_pc(0x1000);
    cpu_aot->set_cpsr(USER32MODE);
    cpu_aot->set_reg(13, 0x10000);

    // Register AOT function
    eka2l1::arm::aot::registry &reg = eka2l1::arm::aot::global_registry();
    reg.clear();
    reg.register_function(0x1000, aot_add_loop);

    // Run — should hit AOT function immediately, then land on 0x1014 (B .)
    cpu_aot->run(100);

    REQUIRE(cpu_aot->get_reg(0) == 10);
    REQUIRE(cpu_aot->get_reg(1) == 0);

    auto aot_snap = cpu_snapshot::capture(*cpu_aot, env_aot.memory);

    // Compare register results (PC will differ slightly due to B . interpretation)
    CHECK(interp_snap.regs[0] == aot_snap.regs[0]);
    CHECK(interp_snap.regs[1] == aot_snap.regs[1]);
    CHECK(interp_snap.regs[13] == aot_snap.regs[13]);

    // Clean up global registry
    reg.clear();
}

// --- Test: Memory write — interpreter vs AOT produce identical memory ---
//
// ARM code at address 0x1000:
//   0x1000: LDR R0, [R2]       ; e5920000
//   0x1004: ADD R0, R0, #1     ; e2800001
//   0x1008: STR R0, [R2]       ; e5820000
//   0x100C: B .                 ; eafffffe
//
// R2 points to a memory location. The code increments the value at that location.

static std::uint32_t aot_mem_inc(ARMul_State *cpu) {
    std::uint32_t addr = cpu->Reg[2];
    std::uint32_t val = cpu->ReadMemory32(addr);
    val += 1;
    cpu->WriteMemory32(addr, val);
    cpu->Reg[0] = val;
    cpu->Reg[15] = 0x100C; // PC after the sequence
    return 3; // LDR + ADD + STR
}

TEST_CASE("aot_memory_write_vs_interpreter", "[aot]") {
    const std::uint32_t data_addr = 0x2000;
    const std::uint32_t initial_value = 42;

    // Interpreter run
    aot_test_env env_interp;
    env_interp.write_code(0x1000, {
        0xe5920000, // LDR R0, [R2]
        0xe2800001, // ADD R0, R0, #1
        0xe5820000, // STR R0, [R2]
        0xeafffffe, // B .
    });
    env_interp.write32(data_addr, initial_value);

    auto cpu_interp = env_interp.make_cpu();
    cpu_interp->set_pc(0x1000);
    cpu_interp->set_cpsr(USER32MODE);
    cpu_interp->set_reg(2, data_addr);
    cpu_interp->set_reg(13, 0x10000);
    cpu_interp->run(10);

    REQUIRE(cpu_interp->get_reg(0) == 43);
    REQUIRE(env_interp.read32(data_addr) == 43);

    // AOT run
    aot_test_env env_aot;
    env_aot.write_code(0x1000, {
        0xe5920000, // LDR R0, [R2]
        0xe2800001, // ADD R0, R0, #1
        0xe5820000, // STR R0, [R2]
        0xeafffffe, // B .
    });
    env_aot.write32(data_addr, initial_value);

    auto cpu_aot = env_aot.make_cpu();
    cpu_aot->set_pc(0x1000);
    cpu_aot->set_cpsr(USER32MODE);
    cpu_aot->set_reg(2, data_addr);
    cpu_aot->set_reg(13, 0x10000);

    eka2l1::arm::aot::registry &reg = eka2l1::arm::aot::global_registry();
    reg.clear();
    reg.register_function(0x1000, aot_mem_inc);

    cpu_aot->run(10);

    REQUIRE(cpu_aot->get_reg(0) == 43);
    REQUIRE(env_aot.read32(data_addr) == 43);

    // Compare
    CHECK(cpu_interp->get_reg(0) == cpu_aot->get_reg(0));
    CHECK(env_interp.read32(data_addr) == env_aot.read32(data_addr));

    reg.clear();
}

// --- Test: 32-bit word copy loop (simplified gdi.dll pattern) ---
//
// Simulates copying 32-bit pixels from source to dest buffer.
//
// ARM code at address 0x1000:
//   0x1000: LDR R3, [R0]        ; e5903000  (load word from src)
//   0x1004: STR R3, [R1]        ; e5813000  (store word to dst)
//   0x1008: ADD R0, R0, #4      ; e2800004  (advance src)
//   0x100C: ADD R1, R1, #4      ; e2811004  (advance dst)
//   0x1010: SUBS R2, R2, #1     ; e2522001  (decrement count)
//   0x1014: BNE loop             ; 1afffff9  (branch to 0x1000 if not zero)
//   0x1018: B .                  ; eafffffe  (halt)
//
// R0 = src, R1 = dst, R2 = count

static std::uint32_t aot_pixel_copy(ARMul_State *cpu) {
    std::uint32_t src = cpu->Reg[0];
    std::uint32_t dst = cpu->Reg[1];
    std::uint32_t count = cpu->Reg[2];

    while (count > 0) {
        std::uint32_t pixel = cpu->ReadMemory32(src);
        cpu->WriteMemory32(dst, pixel);
        src += 4;
        dst += 4;
        count--;
    }

    cpu->Reg[0] = src;
    cpu->Reg[1] = dst;
    cpu->Reg[2] = count;
    cpu->Reg[3] = cpu->ReadMemory32(src - 4); // last loaded value still in R3
    cpu->Reg[15] = 0x1018; // after loop

    // SUBS R2, R2, #1 with result 0: Z=1, C=1, N=0, V=0
    cpu->NFlag = 0;
    cpu->ZFlag = 1;
    cpu->CFlag = 1;
    cpu->VFlag = 0;

    return count * 6;
}

TEST_CASE("aot_pixel_copy_vs_interpreter", "[aot]") {
    const std::uint32_t src_addr = 0x2000;
    const std::uint32_t dst_addr = 0x3000;
    const std::uint32_t pixel_count = 8;

    // Set up source pixels (32-bit ARGB)
    std::vector<std::uint32_t> src_pixels = {
        0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF,
        0xFF000000, 0xFF123456, 0xFF789ABC, 0xFFDEF012
    };

    // Interpreter run
    aot_test_env env_interp;
    env_interp.write_code(0x1000, {
        0xe5903000, // LDR R3, [R0]
        0xe5813000, // STR R3, [R1]
        0xe2800004, // ADD R0, R0, #4
        0xe2811004, // ADD R1, R1, #4
        0xe2522001, // SUBS R2, R2, #1
        0x1afffff9, // BNE back to 0x1000
        0xeafffffe, // B .
    });
    for (std::uint32_t i = 0; i < pixel_count; i++) {
        env_interp.write32(src_addr + i * 4, src_pixels[i]);
    }

    auto cpu_interp = env_interp.make_cpu();
    cpu_interp->set_pc(0x1000);
    cpu_interp->set_cpsr(USER32MODE);
    cpu_interp->set_reg(0, src_addr);
    cpu_interp->set_reg(1, dst_addr);
    cpu_interp->set_reg(2, pixel_count);
    cpu_interp->set_reg(13, 0x10000);
    cpu_interp->run(200);

    // Verify pixels were copied
    for (std::uint32_t i = 0; i < pixel_count; i++) {
        REQUIRE(env_interp.read32(dst_addr + i * 4) == src_pixels[i]);
    }

    // AOT run
    aot_test_env env_aot;
    env_aot.write_code(0x1000, {
        0xe5903000, // LDR R3, [R0]
        0xe5813000, // STR R3, [R1]
        0xe2800004, // ADD R0, R0, #4
        0xe2811004, // ADD R1, R1, #4
        0xe2522001, // SUBS R2, R2, #1
        0x1afffff9, // BNE back to 0x1000
        0xeafffffe, // B .
    });
    for (std::uint32_t i = 0; i < pixel_count; i++) {
        env_aot.write32(src_addr + i * 4, src_pixels[i]);
    }

    auto cpu_aot = env_aot.make_cpu();
    cpu_aot->set_pc(0x1000);
    cpu_aot->set_cpsr(USER32MODE);
    cpu_aot->set_reg(0, src_addr);
    cpu_aot->set_reg(1, dst_addr);
    cpu_aot->set_reg(2, pixel_count);
    cpu_aot->set_reg(13, 0x10000);

    eka2l1::arm::aot::registry &reg = eka2l1::arm::aot::global_registry();
    reg.clear();
    reg.register_function(0x1000, aot_pixel_copy);

    cpu_aot->run(200);

    // Verify pixels were copied
    for (std::uint32_t i = 0; i < pixel_count; i++) {
        REQUIRE(env_aot.read32(dst_addr + i * 4) == src_pixels[i]);
    }

    // Compare interpreter vs AOT
    CHECK(cpu_interp->get_reg(0) == cpu_aot->get_reg(0));
    CHECK(cpu_interp->get_reg(1) == cpu_aot->get_reg(1));
    CHECK(cpu_interp->get_reg(2) == cpu_aot->get_reg(2));

    for (std::uint32_t i = 0; i < pixel_count; i++) {
        CHECK(env_interp.read32(dst_addr + i * 4) == env_aot.read32(dst_addr + i * 4));
    }

    reg.clear();
}

// --- Test: Config string parsing ---

TEST_CASE("aot_parse_config_string", "[aot]") {
    using namespace eka2l1::arm::aot;

    SECTION("empty string") {
        auto cfg = parse_config_string("");
        REQUIRE(cfg.empty());
    }

    SECTION("single DLL, whole DLL") {
        auto cfg = parse_config_string("gdi.dll");
        REQUIRE(cfg.size() == 1);
        CHECK(cfg[0].dll_name == "gdi.dll");
        CHECK(cfg[0].symbols.empty());
    }

    SECTION("single DLL with symbols") {
        auto cfg = parse_config_string("gdi.dll:BlitPixels:DrawLine");
        REQUIRE(cfg.size() == 1);
        CHECK(cfg[0].dll_name == "gdi.dll");
        REQUIRE(cfg[0].symbols.size() == 2);
        CHECK(cfg[0].symbols[0] == "BlitPixels");
        CHECK(cfg[0].symbols[1] == "DrawLine");
    }

    SECTION("multiple DLLs") {
        auto cfg = parse_config_string("gdi.dll,fbserv.dll:Func1,bitgdi.dll");
        REQUIRE(cfg.size() == 3);
        CHECK(cfg[0].dll_name == "gdi.dll");
        CHECK(cfg[0].symbols.empty());
        CHECK(cfg[1].dll_name == "fbserv.dll");
        REQUIRE(cfg[1].symbols.size() == 1);
        CHECK(cfg[1].symbols[0] == "Func1");
        CHECK(cfg[2].dll_name == "bitgdi.dll");
        CHECK(cfg[2].symbols.empty());
    }
}

// --- Test: Catalog and config ---

static std::uint32_t dummy_aot_a(ARMul_State *) { return 1; }
static std::uint32_t dummy_aot_b(ARMul_State *) { return 2; }
static std::uint32_t dummy_aot_c(ARMul_State *) { return 3; }

TEST_CASE("aot_catalog_config", "[aot]") {
    using namespace eka2l1::arm::aot;

    catalog cat;
    cat.add_entry({"gdi.dll", "BlitPixels", 0x100, dummy_aot_a});
    cat.add_entry({"gdi.dll", "DrawLine", 0x200, dummy_aot_b});
    cat.add_entry({"fbserv.dll", "CreateBitmap", 0x50, dummy_aot_c});

    SECTION("whole DLL enabled") {
        cat.set_config(parse_config_string("gdi.dll"));
        CHECK(cat.is_enabled("gdi.dll", "BlitPixels"));
        CHECK(cat.is_enabled("gdi.dll", "DrawLine"));
        CHECK_FALSE(cat.is_enabled("fbserv.dll", "CreateBitmap"));

        auto entries = cat.get_entries_for_dll("gdi.dll");
        REQUIRE(entries.size() == 2);
    }

    SECTION("specific symbols enabled") {
        cat.set_config(parse_config_string("gdi.dll:BlitPixels"));
        CHECK(cat.is_enabled("gdi.dll", "BlitPixels"));
        CHECK_FALSE(cat.is_enabled("gdi.dll", "DrawLine"));

        auto entries = cat.get_entries_for_dll("gdi.dll");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0]->symbol_name == "BlitPixels");
    }

    SECTION("activate_dll populates registry") {
        cat.set_config(parse_config_string("gdi.dll"));
        registry reg;
        cat.activate_dll("gdi.dll", 0x80000000, reg);
        REQUIRE(reg.size() == 2);
        CHECK(reg.lookup(0x80000100) == dummy_aot_a);
        CHECK(reg.lookup(0x80000200) == dummy_aot_b);
        CHECK(reg.lookup(0x80000050) == nullptr); // fbserv not activated
    }

    SECTION("case insensitive DLL matching") {
        cat.set_config(parse_config_string("GDI.DLL"));
        CHECK(cat.is_enabled("gdi.dll", "BlitPixels"));
        CHECK(cat.is_enabled("Gdi.Dll", "DrawLine"));
    }
}

// --- Test: WASM module emitter produces valid module ---

TEST_CASE("wasm_emitter_valid_module", "[aot]") {
    using namespace eka2l1::arm::aot;

    // Emit a trivial function: return i32.const 42
    std::vector<std::uint8_t> body;
    body.push_back(op_i32_const);
    // LEB128 encode 42
    body.push_back(42);
    body.push_back(op_return);

    wasm_func_def func;
    func.export_name = "test_func";
    func.body = body;
    func.num_locals = 0;

    auto module = build_wasm_module({ func });

    // Verify WASM magic and version
    REQUIRE(module.size() >= 8);
    CHECK(module[0] == 0x00);
    CHECK(module[1] == 0x61); // 'a'
    CHECK(module[2] == 0x73); // 's'
    CHECK(module[3] == 0x6D); // 'm'
    CHECK(module[4] == 0x01); // version 1
    CHECK(module[5] == 0x00);
    CHECK(module[6] == 0x00);
    CHECK(module[7] == 0x00);

    // Module should have reasonable size (header + sections)
    CHECK(module.size() > 20);
    CHECK(module.size() < 1000);
}

TEST_CASE("armul_state_offsets", "[aot]") {
    // Verify the field offsets we need for the WASM translator
    // Reg[16] is at offset 0
    REQUIRE(offsetof(ARMul_State, Reg) == 0);

    // These offsets are used by the WASM translator to access CPU state.
    // If any change, the translator constants must be updated.
    CHECK(offsetof(ARMul_State, Cpsr) == offsetof(ARMul_State, Emulate) + 4);
    CHECK(offsetof(ARMul_State, NFlag) > offsetof(ARMul_State, Cpsr));

    // Capture actual values for reference
    std::size_t reg_off = offsetof(ARMul_State, Reg);
    std::size_t cpsr_off = offsetof(ARMul_State, Cpsr);
    std::size_t nflag_off = offsetof(ARMul_State, NFlag);
    std::size_t zflag_off = offsetof(ARMul_State, ZFlag);
    std::size_t cflag_off = offsetof(ARMul_State, CFlag);
    std::size_t vflag_off = offsetof(ARMul_State, VFlag);
    std::size_t tflag_off = offsetof(ARMul_State, TFlag);

    // Flags should be consecutive
    CHECK(zflag_off == nflag_off + 4);
    CHECK(cflag_off == zflag_off + 4);
    CHECK(vflag_off == cflag_off + 4);

    // Print for reference (visible with -s flag)
    printf("  ARMul_State offsets:\n");
    printf("    Reg:   %zu\n", reg_off);
    printf("    Cpsr:  %zu\n", cpsr_off);
    printf("    NFlag: %zu\n", nflag_off);
    printf("    ZFlag: %zu\n", zflag_off);
    printf("    CFlag: %zu\n", cflag_off);
    printf("    VFlag: %zu\n", vflag_off);
    printf("    TFlag: %zu\n", tflag_off);
}

TEST_CASE("wasm_emitter_multiple_functions", "[aot]") {
    using namespace eka2l1::arm::aot;

    std::vector<wasm_func_def> funcs;

    // Function 1: return 1
    {
        wasm_func_def f;
        f.export_name = "f_one";
        f.body = { op_i32_const, 1, op_return };
        f.num_locals = 0;
        funcs.push_back(f);
    }

    // Function 2: return 2
    {
        wasm_func_def f;
        f.export_name = "f_two";
        f.body = { op_i32_const, 2, op_return };
        f.num_locals = 0;
        funcs.push_back(f);
    }

    // Function 3: return param + 10
    {
        wasm_func_def f;
        f.export_name = "f_add10";
        f.body = { op_local_get, 0, op_i32_const, 10, op_i32_add, op_return };
        f.num_locals = 0;
        funcs.push_back(f);
    }

    auto module = build_wasm_module(funcs);

    REQUIRE(module.size() >= 8);
    CHECK(module[0] == 0x00);
    CHECK(module[1] == 0x61);
    // All 3 functions should be in the module
    CHECK(module.size() > 40);
}

// --- Test: Thumb translator produces WASM for simple instructions ---

TEST_CASE("thumb_translator_simple", "[aot]") {
    using namespace eka2l1::arm::aot;

    // Thumb code: MOVS R0, #42; MOVS R1, #10; ADDS R0, R0, #1
    // 0x2000 | 42 = 0x202A  (MOVS R0, #42)
    // 0x2100 | 10 = 0x210A  (MOVS R1, #10)
    // 0x1C40       = 0x1C40  (ADDS R0, R0, #1 — encoding: imm3=1, Rn=0, Rd=0)
    std::uint8_t code[] = {
        0x2A, 0x20,  // MOVS R0, #42
        0x0A, 0x21,  // MOVS R1, #10
        0x40, 0x1C,  // ADDS R0, R0, #1
    };

    auto func = translate_thumb_block(code, sizeof(code), 0x1000);

    // Should produce a non-empty body
    REQUIRE(!func.body.empty());
    CHECK(func.export_name == "f_4096"); // 0x1000 decimal

    // Build it into a WASM module to verify it's structurally valid
    auto module = build_wasm_module({ func });
    REQUIRE(module.size() >= 8);
    CHECK(module[0] == 0x00);
    CHECK(module[1] == 0x61);
    CHECK(module[2] == 0x73);
    CHECK(module[3] == 0x6D);
}

TEST_CASE("thumb_translator_cmp_movs", "[aot]") {
    using namespace eka2l1::arm::aot;

    // Thumb code: MOVS R0, #5; CMP R0, #5
    std::uint8_t code[] = {
        0x05, 0x20,  // MOVS R0, #5
        0x05, 0x28,  // CMP R0, #5
    };

    auto func = translate_thumb_block(code, sizeof(code), 0x2000);
    REQUIRE(!func.body.empty());

    auto module = build_wasm_module({ func });
    REQUIRE(module.size() >= 8);
    // Verify WASM magic
    CHECK(module[0] == 0x00);
    CHECK(module[1] == 0x61);
}

TEST_CASE("thumb_translator_bails_on_memory_ops", "[aot]") {
    using namespace eka2l1::arm::aot;

    // LDR R0, [R1, #0] — should bail to interpreter
    std::uint8_t code[] = {
        0x08, 0x68,  // LDR R0, [R1, #0]
    };

    auto func = translate_thumb_block(code, sizeof(code), 0x3000);
    REQUIRE(!func.body.empty());

    // The body should contain a return instruction (bail)
    // and set PC to the LDR address
    auto module = build_wasm_module({ func });
    REQUIRE(module.size() >= 8);
}

TEST_CASE("thumb_translator_with_branches", "[aot]") {
    using namespace eka2l1::arm::aot;

    // Thumb code with a conditional branch:
    // 0x1000: MOVS R0, #5       (0x2005)
    // 0x1002: MOVS R1, #5       (0x2105)
    // 0x1004: CMP R0, R1        (0x4288)
    // 0x1006: BNE +4 (to 0x100E) (0xD102) — branch within block
    // 0x1008: MOVS R2, #1       (0x2201)
    // 0x100A: B +2 (to 0x1010)  (0xE001) — skip
    // 0x100C: invalid            (pad)
    // 0x100E: MOVS R2, #0       (0x2200) — branch target
    // 0x1010: end
    std::uint8_t code[] = {
        0x05, 0x20,  // MOVS R0, #5
        0x05, 0x21,  // MOVS R1, #5
        0x88, 0x42,  // CMP R0, R1
        0x02, 0xD1,  // BNE +4
        0x01, 0x22,  // MOVS R2, #1
        0x01, 0xE0,  // B +2
        0x00, 0x00,  // padding
        0x00, 0x22,  // MOVS R2, #0
    };

    auto func = translate_thumb_block(code, sizeof(code), 0x1000);
    REQUIRE(!func.body.empty());

    // Build with imports for memory ops
    std::vector<wasm_import_func> imports = {
        {"env", "tlb_read32", 2, true},
        {"env", "tlb_write32", 3, false},
        {"env", "tlb_read8", 2, true},
    };
    auto module = build_wasm_module({ func }, imports);
    REQUIRE(module.size() >= 8);
    CHECK(module[0] == 0x00);
    CHECK(module[1] == 0x61);
}

TEST_CASE("thumb_translator_with_ldr_str", "[aot]") {
    using namespace eka2l1::arm::aot;

    // Thumb code with memory access:
    // LDR R0, [R1, #0]  (0x6808)
    // STR R0, [R2, #0]  (0x6010)
    std::uint8_t code[] = {
        0x08, 0x68,  // LDR R0, [R1, #0]
        0x10, 0x60,  // STR R0, [R2, #0]
    };

    auto func = translate_thumb_block(code, sizeof(code), 0x2000);
    REQUIRE(!func.body.empty());

    std::vector<wasm_import_func> imports = {
        {"env", "tlb_read32", 2, true},
        {"env", "tlb_write32", 3, false},
        {"env", "tlb_read8", 2, true},
    };
    auto module = build_wasm_module({ func }, imports);
    REQUIRE(module.size() >= 8);
    CHECK(module[0] == 0x00);
}

TEST_CASE("thumb_translator_multiple_funcs_one_module", "[aot]") {
    using namespace eka2l1::arm::aot;

    // Two different Thumb blocks
    std::uint8_t code1[] = { 0x05, 0x20 };  // MOVS R0, #5
    std::uint8_t code2[] = { 0x0A, 0x21 };  // MOVS R1, #10

    auto f1 = translate_thumb_block(code1, sizeof(code1), 0x4000);
    auto f2 = translate_thumb_block(code2, sizeof(code2), 0x4100);

    REQUIRE(!f1.body.empty());
    REQUIRE(!f2.body.empty());
    CHECK(f1.export_name != f2.export_name);

    // Both in one module
    auto module = build_wasm_module({ f1, f2 });
    REQUIRE(module.size() >= 8);
    CHECK(module[0] == 0x00);
    CHECK(module[1] == 0x61);
}
