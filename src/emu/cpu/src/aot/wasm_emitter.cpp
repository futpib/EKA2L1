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

#include <cpu/aot/wasm_emitter.h>
#include <cstring>

namespace eka2l1::arm::aot {

    static void leb128(std::vector<std::uint8_t> &out, std::uint32_t value) {
        do {
            std::uint8_t b = value & 0x7F;
            value >>= 7;
            if (value != 0) b |= 0x80;
            out.push_back(b);
        } while (value != 0);
    }

    static void sleb128(std::vector<std::uint8_t> &out, std::int32_t value) {
        bool more = true;
        while (more) {
            std::uint8_t b = value & 0x7F;
            value >>= 7;
            if ((value == 0 && (b & 0x40) == 0) || (value == -1 && (b & 0x40) != 0))
                more = false;
            else
                b |= 0x80;
            out.push_back(b);
        }
    }

    static void emit_str(std::vector<std::uint8_t> &out, const std::string &s) {
        leb128(out, static_cast<std::uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }

    static void emit_section(std::vector<std::uint8_t> &module, std::uint8_t id, const std::vector<std::uint8_t> &content) {
        module.push_back(id);
        leb128(module, static_cast<std::uint32_t>(content.size()));
        module.insert(module.end(), content.begin(), content.end());
    }

    std::vector<std::uint8_t> build_wasm_module(
        const std::vector<wasm_func_def> &funcs,
        const std::vector<wasm_import_func> &imports)
    {
        std::vector<std::uint8_t> module;

        // Header
        const std::uint8_t header[] = { 0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00 };
        module.insert(module.end(), header, header + 8);

        const std::uint32_t num_imports = static_cast<std::uint32_t>(imports.size());
        const std::uint32_t num_funcs = static_cast<std::uint32_t>(funcs.size());

        // Type indices: 0..num_imports-1 for import types, then num_imports for the AOT func type
        // All AOT functions share the same type: (i32) -> (i32)
        const std::uint32_t aot_type_idx = num_imports;
        const std::uint32_t num_types = num_imports + 1;

        // === Section 1: Type ===
        {
            std::vector<std::uint8_t> sec;
            leb128(sec, num_types);

            for (auto &imp : imports) {
                sec.push_back(0x60); // functype
                leb128(sec, imp.param_count);
                for (std::uint8_t j = 0; j < imp.param_count; j++)
                    sec.push_back(type_i32);
                leb128(sec, imp.has_result ? 1u : 0u);
                if (imp.has_result)
                    sec.push_back(type_i32);
            }

            // AOT function type: (i32) -> (i32)
            sec.push_back(0x60);
            leb128(sec, 1);
            sec.push_back(type_i32);
            leb128(sec, 1);
            sec.push_back(type_i32);

            emit_section(module, 1, sec);
        }

        // === Section 2: Import ===
        {
            std::vector<std::uint8_t> sec;
            leb128(sec, num_imports + 1); // function imports + memory

            // Import shared memory (required for i32.load/store on state_ptr)
            emit_str(sec, "env");
            emit_str(sec, "memory");
            sec.push_back(0x02); // memory
            // Shared memory limits: flags=0x03 (has_max | shared), min, max
            sec.push_back(0x03); // flags: has_max + shared
            leb128(sec, 256);    // min pages
            leb128(sec, 65536);  // max pages

            // Import functions
            for (std::uint32_t i = 0; i < num_imports; i++) {
                emit_str(sec, imports[i].module_name);
                emit_str(sec, imports[i].func_name);
                sec.push_back(0x00); // func
                leb128(sec, i);      // type index
            }

            emit_section(module, 2, sec);
        }

        // === Section 3: Function ===
        {
            std::vector<std::uint8_t> sec;
            leb128(sec, num_funcs);
            for (std::uint32_t i = 0; i < num_funcs; i++) {
                leb128(sec, aot_type_idx);
            }
            emit_section(module, 3, sec);
        }

        // === Section 7: Export ===
        {
            std::vector<std::uint8_t> sec;
            leb128(sec, num_funcs);
            for (std::uint32_t i = 0; i < num_funcs; i++) {
                emit_str(sec, funcs[i].export_name);
                sec.push_back(0x00); // func export
                leb128(sec, num_imports + i); // func index (after imports)
            }
            emit_section(module, 7, sec);
        }

        // === Section 10: Code ===
        {
            std::vector<std::uint8_t> sec;
            leb128(sec, num_funcs);

            for (auto &func : funcs) {
                std::vector<std::uint8_t> body;

                // Locals — up to 3 groups: i32, f32, f64
                {
                    std::uint32_t num_groups = 0;
                    if (func.num_locals > 0) num_groups++;
                    if (func.num_f32_locals > 0) num_groups++;
                    if (func.num_f64_locals > 0) num_groups++;
                    leb128(body, num_groups);
                    if (func.num_locals > 0) {
                        leb128(body, func.num_locals);
                        body.push_back(type_i32);
                    }
                    if (func.num_f32_locals > 0) {
                        leb128(body, func.num_f32_locals);
                        body.push_back(type_f32);
                    }
                    if (func.num_f64_locals > 0) {
                        leb128(body, func.num_f64_locals);
                        body.push_back(type_f64);
                    }
                }

                // Body bytecode
                body.insert(body.end(), func.body.begin(), func.body.end());

                // End
                body.push_back(op_end);

                // Emit body with size prefix
                leb128(sec, static_cast<std::uint32_t>(body.size()));
                sec.insert(sec.end(), body.begin(), body.end());
            }

            emit_section(module, 10, sec);
        }

        return module;
    }
}
