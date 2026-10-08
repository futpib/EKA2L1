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
#include <map>
#include <set>
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
        op_select = 0x1B,

        op_local_get = 0x20,
        op_local_set = 0x21,
        op_local_tee = 0x22,

        op_i32_load = 0x28,
        op_i32_load16_s = 0x2E,
        op_i32_load16_u = 0x2F,
        op_i32_load8_s = 0x2C,
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
        op_i32_div_u = 0x6E,
        op_i32_and = 0x71,
        op_i32_or = 0x72,
        op_i32_xor = 0x73,
        op_i32_shl = 0x74,
        op_i32_shr_s = 0x75,
        op_i32_shr_u = 0x76,
        op_i32_rotr = 0x78,
        op_i32_clz = 0x67,

        op_i64_const = 0x42,
        op_i64_add = 0x7C,
        op_i64_mul = 0x7E,
        op_i64_or = 0x84,
        op_i64_shl = 0x86,
        op_i64_shr_u = 0x88,
        op_i32_wrap_i64 = 0xA7,
        op_i64_extend_i32_s = 0xAC,
        op_i64_extend_i32_u = 0xAD,

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
        op_f64_load = 0x2B,
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

    enum class source_kind : std::uint32_t {
        unknown, guest, state, memory_check, guest_memory, accounting, dispatch, helper
    };
    struct source_mark {
        std::uint32_t offset, pc;
        source_kind kind;
    };
    using source_marks = std::vector<source_mark>;

    // Copy byte-range provenance through insertion, outlining and deletion.
    inline void copy_source_marks(const source_marks &from, std::size_t begin,
        std::size_t end, std::size_t destination, source_marks &to) {
        if (begin == end || from.empty()) return;
        source_mark active{0, 0, source_kind::unknown};
        for (const auto &mark : from) {
            if (mark.offset <= begin) active = mark;
            else break;
        }
        active.offset = static_cast<std::uint32_t>(destination);
        to.push_back(active);
        for (auto mark : from) if (mark.offset > begin && mark.offset < end) {
            mark.offset = static_cast<std::uint32_t>(destination + mark.offset - begin);
            to.push_back(mark);
        }
    }
    struct source_emission {
        source_marks *marks = nullptr;
        std::uint32_t pc = 0;
        source_kind kind = source_kind::unknown;
        void mark(std::size_t offset) const {
#ifdef EKA2L1_AOT_SOURCE_MAPS
            if (marks && (marks->empty() || marks->back().pc != pc || marks->back().kind != kind))
                marks->push_back({static_cast<std::uint32_t>(offset), pc, kind});
#endif
        }
    };
    struct source_scope {
        source_emission &emission;
        source_kind previous;
        source_scope(source_emission &value, source_kind kind)
            : emission(value), previous(value.kind) { emission.kind = kind; }
        ~source_scope() { emission.kind = previous; }
    };

    // Describes one function to include in the WASM module.
    struct wasm_func_def {
        std::string export_name;              // e.g. "f_80464C14"
        std::vector<std::uint8_t> body;       // WASM bytecode (without end opcode)
        std::uint32_t num_locals;             // i32 locals (beyond the state_ptr param)
        std::uint32_t num_f32_locals = 0;     // f32 locals (after i32 locals)
        std::uint32_t num_f64_locals = 0;     // f64 locals (after f32 locals)
        std::uint32_t num_prefix_i64_locals = 0; // before i32 scratch/cache locals
        // Optional private cold callee. The call operand at this body offset
        // reserves five unsigned-LEB bytes; the module builder supplies its
        // final index after all public functions, preserving sibling indices.
        std::shared_ptr<wasm_func_def> outlined_callee;
        std::uint32_t outlined_call_offset = 0;
        struct private_call {
            std::shared_ptr<wasm_func_def> callee;
            std::uint32_t call_offset;
        };
        std::vector<private_call> outlined_calls;
        source_marks sources;
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
