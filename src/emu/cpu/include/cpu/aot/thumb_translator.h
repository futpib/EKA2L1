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
#include <cstddef>
#include <cstdint>
#include <cstring>
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
        // Addresses where execution may resume after a bail-out call
        // (the instruction immediately after a BLX Rm, BL Rm, or non-sibling
        // BL imm). The caller can register these as additional AOT entry
        // points so that when the interpreter returns from the external call
        // it can dispatch back into AOT at the resume address.
        std::vector<std::uint32_t> resume_points;
        // Local B/B<cond> targets discovered within this block. Registering
        // them as separate AOT entries means that when the interpreter takes
        // over after a forward-branch bail, it can dispatch back into AOT
        // at the branch target.
        std::vector<std::uint32_t> branch_targets;
        // One past the address of the last instruction the decoder actually
        // emitted code for. For a function that ends at POP {PC} at 0x1002
        // and stops decoding there, this is 0x1004. Used by callers to size
        // AOT coverage and by tests to verify the decoder didn't walk into
        // trailing literal pools.
        std::uint32_t end_address = 0;
        // Number of early-exit bails emitted into the function body. Each
        // bail is a point where AOT execution yields back to the
        // interpreter (unsupported wide insn, unresolved BL/BLX target,
        // out-of-block branch, etc). Tests use this as a proxy for "how
        // much of this function actually runs in WASM" — a well-covered
        // function has 0-2 bails (typically just the return path),
        // a poorly-covered one bails on every other instruction.
        std::uint32_t bail_count = 0;
    };

    // Map of ARM addresses to WASM function indices for BL target inlining.
    // When the translator encounters a BL, if the target address is in this
    // map, it emits a direct `call` to that function index instead of bailing
    // to the interpreter.
    using sibling_map = std::unordered_map<std::uint32_t, std::uint32_t>;

    // Read-only view of a DLL's entire code section. Used by the translator
    // to peek at arbitrary addresses (e.g. resolving BLX imm veneers that
    // sit outside the current function's code window).
    struct code_window {
        const std::uint8_t *host;   // host pointer to base
        std::uint32_t base;         // ARM virtual base address
        std::uint32_t size;         // bytes available

        // Returns true and copies `len` bytes into `out` if [addr, addr+len)
        // is entirely inside the window; false otherwise.
        bool read(std::uint32_t addr, void *out, std::size_t len) const {
            if (!host || addr < base || len > size) return false;
            std::uint32_t off = addr - base;
            if (off + len > size) return false;
            std::memcpy(out, host + off, len);
            return true;
        }
    };

    // Translate a block of Thumb code into a WASM function body.
    // The function takes one i32 parameter (state_ptr) and returns i32 (instruction count).
    //
    // start_address: the ARM address of the first instruction (used for branch target resolution)
    // code: pointer to Thumb bytecode
    // code_size: size in bytes
    // siblings: optional map of address → WASM func index for BL target inlining
    // dll_code: optional full-DLL code window for resolving BLX imm veneers
    //
    // Returns a wasm_func_def ready to be included in a WASM module.
    // Returns empty body on failure.
    // If complete is false, the function bails to the interpreter partway through.
    translate_result translate_thumb_block(
        const std::uint8_t *code,
        std::size_t code_size,
        std::uint32_t start_address,
        const sibling_map *siblings = nullptr,
        const code_window *dll_code = nullptr);
}
