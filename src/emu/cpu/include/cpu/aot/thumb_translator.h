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

#include <cpu/aot/wasm_emitter.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eka2l1::arm::aot {
    // ARMul_State field offsets (must match the actual struct layout)
    struct state_offsets {
        static constexpr std::uint32_t REG = 0;          // Reg[0]
        static constexpr std::uint32_t CPSR = 784;
        static constexpr std::uint32_t NFLAG = 804;
        static constexpr std::uint32_t ZFLAG = 808;
        static constexpr std::uint32_t CFLAG = 812;
        static constexpr std::uint32_t VFLAG = 816;
        static constexpr std::uint32_t TFLAG = 828;

        static constexpr std::uint32_t reg(int n) { return REG + n * 4; }
        static constexpr std::uint32_t PC = REG + 15 * 4;
        static constexpr std::uint32_t LR = REG + 14 * 4;
        static constexpr std::uint32_t SP = REG + 13 * 4;
    };

    struct translate_result {
        wasm_func_def func;
        bool complete;  // true if entire block was translated without bailing
    };

    // Map of ARM addresses to WASM function indices for BL target inlining.
    // When the translator encounters a BL, if the target address is in this
    // map, it emits a direct `call` to that function index instead of bailing
    // to the interpreter.
    using sibling_map = std::unordered_map<std::uint32_t, std::uint32_t>;

    // Translate a block of Thumb code into a WASM function body.
    // The function takes one i32 parameter (state_ptr) and returns i32 (instruction count).
    //
    // start_address: the ARM address of the first instruction (used for branch target resolution)
    // code: pointer to Thumb bytecode
    // code_size: size in bytes
    // siblings: optional map of address → WASM func index for BL target inlining
    //
    // Returns a wasm_func_def ready to be included in a WASM module.
    // Returns empty body on failure.
    // If complete is false, the function bails to the interpreter partway through.
    translate_result translate_thumb_block(
        const std::uint8_t *code,
        std::size_t code_size,
        std::uint32_t start_address,
        const sibling_map *siblings = nullptr);
}
