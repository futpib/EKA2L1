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

#include <cpu/aot/thumb_translator.h>

namespace eka2l1::arm::aot {
    // Translate a block of ARM-mode code into a WASM function body.
    // ARM instructions are 32-bit fixed-width with condition codes in bits [31:28].
    //
    // start_address: the ARM address of the first instruction (word-aligned)
    // code: pointer to ARM bytecode (little-endian 32-bit words)
    // code_size: size in bytes (must be multiple of 4)
    // siblings: optional map of address → WASM func index for BL target inlining
    // dll_code: optional full-DLL code window for resolving veneers
    //
    // Returns a translate_result (same as Thumb translator).
    translate_result translate_arm_block(
        const std::uint8_t *code,
        std::size_t code_size,
        std::uint32_t start_address,
        const sibling_map *siblings = nullptr,
        const code_window *dll_code = nullptr);
}
