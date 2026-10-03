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

#pragma once

#include <cstdint>
#include <memory>
#include <cpu/aot/aot_registry.h>
#include <vector>
#include <string>

struct ARMul_State;

namespace eka2l1::arm::aot {
    // Generated SVC returns a pending trap to the outer loop, where the exact
    // cumulative instruction count and kernel callback contract are available.
    inline bool compiled_svc_enabled = false;
    // Compile deferred scalar/span misses through the existing memory helpers.
    // Frozen before initialization; disabled until coverage and timing acceptance.
    inline bool compiled_memory_misses = false;
    // Frozen before init: 0 sampled, 1 first-use ROM/RAM, 2 first-use RAM.
    // Mode 2 retains ROM hotness filtering; mode 3 recycles the bounded ROM cache.
    inline unsigned synchronous_compilation = 0;
    inline constexpr std::uint32_t svc_pending = 0x80000000u;
    inline constexpr std::uint32_t svc_taken = 0x40000000u;
    inline constexpr std::uint32_t svc_page_end = 0x20000000u;
    // Opt-in immutable ROM leaf fusion; frozen before CPU initialization.
    extern bool rom_inline_leaves;
    extern bool rom_bounded_calls;
    extern bool rom_dispatch_enabled;
    extern bool rom_state_cohorts;
    extern bool dynamic_rom_cohorts;
    // Frozen before execution: bits 1/2/4 select verification lookup, trusted cache, quiet outer loop;
    // bit 8 selects the sparse immutable-ROM registry index.
    inline unsigned hotpath_policy = 0;
    std::vector<std::uint8_t> resolve_rom_leaf(const std::uint8_t *host,
        std::uint32_t base, std::uint32_t size, std::uint32_t target);
    extern bool diagnostics_enabled;
    // Opt-in runtime layout experiment; effective only when emitted code has no interval guards.
    extern bool omit_guard_publication;
    extern bool hot_compilation_enabled;
    extern bool ram_compilation_enabled;
    extern bool chaining_enabled;
    struct compiled_run { std::uint32_t instructions = 0, blocks = 0; };
    std::uint32_t execute_single(ARMul_State *cpu, aot_func function);
    compiled_run execute_chain(ARMul_State *cpu, aot_func function);
    aot_func lookup_compiled(ARMul_State *cpu);
    void invalidate_ram_code(std::uint32_t address, std::size_t size);
    void configure_hot_rom(const std::uint8_t *host, std::uint32_t base, std::uint32_t size, bool enabled);
    void observe_hot_pc(ARMul_State *cpu);
    extern bool validation_running;
    void validation_begin(ARMul_State *cpu);
    void validation_end(ARMul_State *cpu, std::uint32_t count);
    // Stage WASM module bytes for deferred instantiation.
    // The actual WebAssembly.Instance + addFunction calls happen on the
    // first AOT lookup, which runs on the emulator worker thread where
    // the function table is accessible.
    void stage_aot_module(
        std::vector<std::uint8_t> wasm_bytes,
        const std::string &dll_name, std::shared_ptr<void> keepalive = {});

    // Called from the AOT dispatch path (on the worker thread) to
    // instantiate any staged modules. Returns true if modules were
    // instantiated.
    bool instantiate_staged_modules();
    // Atomic observer value; safe to read from the browser main thread.
    std::uint64_t compiled_function_count();
}
