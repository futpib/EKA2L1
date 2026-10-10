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
#include <functional>

namespace eka2l1::arm::aot {
    // Shared instruction support, fixed before any module is translated.
    inline bool arm_exclusive_memory = false;
    inline bool arm_indirect_calls = true;
    inline bool supported_exclusive_word(std::uint32_t op) {
        const bool load = (op & 0x0ff00fff) == 0x01900f9f;
        const bool store = (op & 0x0ff00ff0) == 0x01800f90;
        return (op >> 28) < 15 && (load || store)
            && ((op >> 16) & 15) != 15 && ((op >> 12) & 15) != 15
            && (!store || (op & 15) != 15);
    }
    // Selected before translation. Numeric IDs remain stable for saved runs.
    enum class arm_ir_policy { configured = -1, disabled = 0, invariant_reads = 4, invariant_writes = 5, read_spans = 6, write_spans = 7 };

    inline bool parse_arm_ir_policy(const char *text, arm_ir_policy &out) {
        if (!text) return false;
        if ((text[0] == '0' || (text[0] >= '4' && text[0] <= '7')) && !text[1]) {
            out = static_cast<arm_ir_policy>(text[0] - '0'); return true;
        }
        return false;
    }

    using leaf_resolver = std::function<std::vector<std::uint8_t>(std::uint32_t)>;

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
        const code_window *dll_code = nullptr, bool bounded = false, bool stop_after_store = false, bool cache_registers = false, bool region = false, const leaf_resolver *leaves = nullptr, bool defer_memory = false, arm_ir_policy ir_policy = arm_ir_policy::configured);
}
