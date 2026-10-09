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
#include <atomic>
#include <memory>
#include <cpu/aot/aot_registry.h>
#include <vector>
#include <string>

struct ARMul_State;
namespace eka2l1::arm { class core; }

namespace eka2l1::arm::aot {
    // Generated SVC returns a pending trap to the outer loop for the kernel
    // callback. A trap returns zero to stop the compiled chain.
    inline bool compiled_svc_enabled = true;
    inline bool sparse_rom_lookup_enabled = true;
    inline constexpr std::uint32_t svc_pending = 0x80000000u;
    inline constexpr std::uint32_t svc_taken = 0x40000000u;
    inline constexpr std::uint32_t svc_page_end = 0x20000000u;
    // A proved immutable BX LR / Thumb POP return, with zero or one low
    // register. Bits 24..27 encode that register (8 means only PC).
    inline constexpr std::uint32_t svc_return = 0x10000000u;
    inline constexpr unsigned svc_return_register_shift = 24;
    // Frozen before execution: 0 general lookup, 2 trusted cache specialization.
    inline unsigned hotpath_policy = 0;
    extern common::diagnostics::flag diagnostics_enabled;
    extern bool hot_compilation_enabled;
    extern bool ram_compilation_enabled;
    extern bool chaining_enabled;
    struct compiled_run { std::uint32_t progress = 0; };
    std::uint32_t execute_single(ARMul_State *cpu, aot_func function);
    compiled_run execute_chain(ARMul_State *cpu, aot_func function);
    aot_func lookup_compiled(ARMul_State *cpu);
    void invalidate_ram_code(std::uint32_t address, std::size_t size);
    void configure_hot_rom(const std::uint8_t *host, std::uint32_t base, std::uint32_t size, bool enabled);
    void observe_hot_pc(ARMul_State *cpu);
    // Loader notifications contain relocated entry points, never retained guest
    // pointers. Translation/installation happens on the owning CPU worker.
    void queue_precompile_image(std::uint32_t space, std::uint32_t base,
        const std::uint8_t *bytes, std::uint32_t size, std::vector<std::uint32_t> entries);
    void prepare_compiled_code(core &cpu);
    struct compilation_counters {
        std::atomic<std::uint64_t> translation_us{0}, emission_us{0}, installation_us{0};
        std::atomic<std::uint64_t> eager_functions{0}, eager_passes{0};
    };
    extern compilation_counters compilation;

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
