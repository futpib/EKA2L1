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

#include <system/aot_setup.h>
#include <system/epoc.h>

#include <cpu/aot/aot_registry.h>
#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/thumb_translator.h>
#include <cpu/aot/wasm_emitter.h>
#include <kernel/kernel.h>
#include <kernel/codeseg.h>
#include <mem/mem.h>
#include <common/log.h>

namespace eka2l1::arm::aot {
    // AOT config: which functions to translate per DLL.
    // Each entry is (dll_name, list of {code_offset, size} ranges to translate).
    // If ranges is empty, nothing is translated (just a placeholder).
    struct aot_func_range {
        std::uint32_t code_offset; // offset from DLL code base
        std::uint32_t size;        // size in bytes
    };

    struct aot_dll_entry {
        std::string dll_name;
        std::vector<aot_func_range> ranges;
    };

    // Hardcoded AOT targets. The hot function in gdi.dll's CTypefaceStore
    // font cache lookup. Code offsets relative to gdi.dll code_address.
    // The code_address varies by ROM (e.g., 0x80460738 or 0x80452768).
    // We use offsets from code base so they work regardless of load address.
    //
    // The hot inner loop (0x80464C38-0x80464C80 in one ROM) is ~40% of CPU.
    // It contains: CMP, BGT, MOVS, LDR, LSLS, ADDS, BNE, STR, B — all supported.
    //
    // We translate the full function (PUSH through final B) so the AOT
    // handles setup + loop + teardown. The translator bails on BLX (the
    // one subroutine call), letting the interpreter handle it, then the
    // loop runs in AOT on subsequent dispatch hits.
    static std::vector<aot_dll_entry> get_default_aot_entries() {
        return {
            {
                "gdi.dll",
                {
                    // Full function: from PUSH to final B (return)
                    // 0x80464C14 - 0x80460738 = 0x44DC, size = 0x80464C90 - 0x80464C14 = 0x7C
                    { 0x44DC, 0x7C },
                    // Also register at the inner loop entry (after the BLX call returns)
                    // so the dispatch hook catches it when re-entering the loop.
                    // 0x80464C34 - 0x80460738 = 0x44FC, to 0x80464C90 = 0x4558
                    // Size = 0x4558 - 0x44FC = 0x5C
                    { 0x44FC, 0x5C }
                }
            }
        };
    }

    void initialize(eka2l1::system *sys, const std::string &config_override) {
        if (config_override == "none") {
            LOG_INFO(KERNEL, "AOT: disabled by config");
            return;
        }

        kernel_system *kern = sys->get_kernel_system();
        if (!kern) {
            LOG_WARN(KERNEL, "AOT: kernel not available, skipping");
            return;
        }

        auto aot_entries = get_default_aot_entries();
        LOG_INFO(KERNEL, "AOT: {} DLL(s) configured for translation", aot_entries.size());

        kern->register_codeseg_loaded_callback(
            [aot_entries](const std::string &lib_name, kernel::process *proc, codeseg_ptr seg) {
                // Check if this DLL has AOT entries
                const aot_dll_entry *dll_entry = nullptr;
                for (const auto &entry : aot_entries) {
                    // Case-insensitive compare
                    std::string lib_lower = lib_name;
                    std::string entry_lower = entry.dll_name;
                    for (auto &c : lib_lower) c = std::tolower(static_cast<unsigned char>(c));
                    for (auto &c : entry_lower) c = std::tolower(static_cast<unsigned char>(c));
                    if (lib_lower == entry_lower) {
                        dll_entry = &entry;
                        break;
                    }
                }

                if (!dll_entry || dll_entry->ranges.empty()) {
                    return;
                }

                address code_run_addr = seg->get_code_run_addr(proc, nullptr);
                LOG_INFO(KERNEL, "AOT: translating {} at code base 0x{:08X}", lib_name, code_run_addr);

                // Get pointer to the DLL's code in host memory
                std::uint8_t *code_base_ptr = nullptr;
                seg->get_code_run_addr(proc, &code_base_ptr);

                if (!code_base_ptr) {
                    LOG_ERROR(KERNEL, "AOT: cannot get code pointer for {}", lib_name);
                    return;
                }

                // Translate each configured function range
                std::vector<wasm_func_def> funcs;

                for (const auto &range : dll_entry->ranges) {
                    const std::uint8_t *func_code = code_base_ptr + range.code_offset;
                    std::uint32_t func_addr = code_run_addr + range.code_offset;

                    LOG_INFO(KERNEL, "AOT:   translating 0x{:08X} ({} bytes)", func_addr, range.size);

                    auto func = translate_thumb_block(func_code, range.size, func_addr);
                    if (func.body.empty()) {
                        LOG_WARN(KERNEL, "AOT:   translation produced empty body for 0x{:08X}", func_addr);
                        continue;
                    }

                    funcs.push_back(std::move(func));
                }

                if (funcs.empty()) {
                    LOG_WARN(KERNEL, "AOT: no functions translated for {}", lib_name);
                    return;
                }

                // Build WASM module with all translated functions
                std::vector<wasm_import_func> imports = {
                    {"env", "tlb_read32", 2, true},
                    {"env", "tlb_write32", 3, false},
                    {"env", "tlb_read8", 2, true},
                };

                auto wasm_bytes = build_wasm_module(funcs, imports);
                LOG_INFO(KERNEL, "AOT: built WASM module for {} ({} bytes, {} functions)",
                    lib_name, wasm_bytes.size(), funcs.size());

                // Instantiate the module (only works on Emscripten)
                int count = instantiate_aot_module(wasm_bytes, lib_name);
                if (count > 0) {
                    LOG_INFO(KERNEL, "AOT: {} functions active for {}", count, lib_name);
                } else {
                    LOG_WARN(KERNEL, "AOT: instantiation returned 0 for {} (expected on native)", lib_name);
                }
            }
        );

        LOG_INFO(KERNEL, "AOT: initialized, waiting for DLL loads");
    }
}
