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

#include <common/diagnostics.h>
#include <cstdint>
#include <memory>
#include <cpu/aot/aot_registry.h>
#include <vector>
#include <string>

struct ARMul_State;

namespace eka2l1::arm::aot {
    // Frozen before execution: 0 general lookup, 2 trusted cache specialization.
    inline unsigned hotpath_policy = 0;
    extern common::diagnostics::flag diagnostics_enabled;
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
        const std::string &dll_name);

    // Called from the AOT dispatch path (on the worker thread) to
    // instantiate any staged modules. Returns true if modules were
    // instantiated.
    bool instantiate_staged_modules();
    // Atomic observer value; safe to read from the browser main thread.
    std::uint64_t compiled_function_count();
}
