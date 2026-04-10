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
#include <string>
#include <vector>

namespace eka2l1::arm::aot {
    // WASM opcodes used by the translator
    enum wasm_op : std::uint8_t {
        op_unreachable = 0x00,
        op_block = 0x02,
        op_loop = 0x03,
        op_if = 0x04,
        op_else = 0x05,
        op_end = 0x0B,
        op_br = 0x0C,
        op_br_if = 0x0D,
        op_br_table = 0x0E,
        op_return = 0x0F,
        op_call = 0x10,

        op_drop = 0x1A,

        op_local_get = 0x20,
        op_local_set = 0x21,
        op_local_tee = 0x22,

        op_i32_load = 0x28,
        op_i32_load16_u = 0x2F,
        op_i32_load8_u = 0x2D,
        op_i32_store = 0x36,
        op_i32_store16 = 0x3B,
        op_i32_store8 = 0x3A,

        op_i32_const = 0x41,

        op_i32_eqz = 0x45,
        op_i32_eq = 0x46,
        op_i32_ne = 0x47,
        op_i32_lt_s = 0x48,
        op_i32_lt_u = 0x49,
        op_i32_gt_s = 0x4A,
        op_i32_gt_u = 0x4B,
        op_i32_le_s = 0x4C,
        op_i32_le_u = 0x4D,
        op_i32_ge_s = 0x4E,
        op_i32_ge_u = 0x4F,

        op_i32_add = 0x6A,
        op_i32_sub = 0x6B,
        op_i32_mul = 0x6C,
        op_i32_and = 0x71,
        op_i32_or = 0x72,
        op_i32_xor = 0x73,
        op_i32_shl = 0x74,
        op_i32_shr_s = 0x75,
        op_i32_shr_u = 0x76,
        op_i32_rotr = 0x78,
        op_i32_clz = 0x67,

        // Float opcodes for VFP support
        op_f32_const = 0x43,
        op_f64_const = 0x44,
        op_f32_abs = 0x8B,
        op_f32_neg = 0x8C,
        op_f32_sqrt = 0x91,
        op_f32_add = 0x92,
        op_f32_sub = 0x93,
        op_f32_mul = 0x94,
        op_f32_div = 0x95,
        op_f64_abs = 0x99,
        op_f64_neg = 0x9A,
        op_f64_sqrt = 0x9F,
        op_f64_add = 0xA0,
        op_f64_sub = 0xA1,
        op_f64_mul = 0xA2,
        op_f64_div = 0xA3,
        op_i32_trunc_f32_s = 0xA8,
        op_i32_trunc_f32_u = 0xA9,
        op_i32_trunc_f64_s = 0xAA,
        op_i32_trunc_f64_u = 0xAB,
        op_f32_convert_i32_s = 0xB2,
        op_f32_convert_i32_u = 0xB3,
        op_f64_convert_i32_s = 0xB7,
        op_f64_convert_i32_u = 0xB8,
        op_f64_promote_f32 = 0xBB,
        op_f32_demote_f64 = 0xB6,
        op_i32_reinterpret_f32 = 0xBC,
        op_f32_reinterpret_i32 = 0xBE,
        op_f32_load = 0x2A,
        op_f64_load = 0x2C,
        op_f32_store = 0x38,
        op_f64_store = 0x39,
        op_f32_eq = 0x5B,
        op_f32_lt = 0x5D,
        op_f32_gt = 0x5E,
        op_f64_eq = 0x61,
        op_f64_lt = 0x63,
        op_f64_gt = 0x64,
    };

    enum wasm_valtype : std::uint8_t {
        type_i32 = 0x7F,
        type_i64 = 0x7E,
        type_f32 = 0x7D,
        type_f64 = 0x7C,
        type_void = 0x40,
    };

    // Describes one function to include in the WASM module.
    struct wasm_func_def {
        std::string export_name;              // e.g. "f_80464C14"
        std::vector<std::uint8_t> body;       // WASM bytecode (without end opcode)
        std::uint32_t num_locals;             // i32 locals (beyond the state_ptr param)
        std::uint32_t num_f32_locals = 0;     // f32 locals (after i32 locals)
        std::uint32_t num_f64_locals = 0;     // f64 locals (after f32 locals)
    };

    // Describes an imported function.
    struct wasm_import_func {
        std::string module_name;
        std::string func_name;
        std::uint8_t param_count;  // all i32
        bool has_result;           // i32 if true
    };

    // Build a complete WASM module containing multiple functions.
    // All functions have signature (state_ptr: i32) -> (i32).
    // The module imports shared memory from the host.
    std::vector<std::uint8_t> build_wasm_module(
        const std::vector<wasm_func_def> &funcs,
        const std::vector<wasm_import_func> &imports = {});
}
