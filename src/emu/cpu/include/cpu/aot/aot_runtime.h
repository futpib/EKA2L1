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
#include <vector>
#include <string>

struct ARMul_State;

namespace eka2l1::arm::aot {
    extern bool diagnostics_enabled;
    extern bool hot_compilation_enabled;
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
}
