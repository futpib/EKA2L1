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
#include <mem/mem.h>
#include <loader/rom.h>
#include <common/log.h>

#include <cstdlib>
#include <cstring>
#include <set>

namespace eka2l1::arm::aot {
    // AOT target: DLL identified by UID3, with list of ordinals to translate.
    // Empty ordinals = translate all exports.
    // Override with AOT_MAX_EXPORTS env var (-1 = unlimited, 0..N = limit).
    static int get_max_exports() {
        const char *env = std::getenv("AOT_MAX_EXPORTS");
        if (env) return std::atoi(env);
        return -1; // default: unlimited
    }

    struct aot_target {
        std::uint32_t uid3;
        std::string display_name;
        std::vector<std::uint32_t> ordinals; // 1-based; empty = all
    };

    static std::vector<aot_target> get_targets() {
        return {
            // FntStore.dll — translate all supported exports
            { 0x10003B1A, "FntStore.dll", {} },
        };
    }

    // ROM image header layout (must match loader/romimage.h)
    struct rom_image_header_raw {
        std::uint32_t uid1, uid2, uid3, uid_checksum;
        std::uint32_t entry_point;
        std::uint32_t code_address;
        std::uint32_t data_address;
        std::int32_t  code_size;
        std::int32_t  text_size;
        std::int32_t  data_size;
        std::int32_t  bss_size;
        std::int32_t  heap_minimum_size;
        std::int32_t  heap_maximum_size;
        std::int32_t  stack_size;
        std::uint32_t dll_ref_table_address;
        std::int32_t  export_dir_count;
        std::uint32_t export_dir_address;
    };

    static constexpr std::uint32_t DLL_UID1 = 0x10000079;

    void initialize(eka2l1::system *sys, const std::string &config_override) {
        if (config_override == "none") {
            LOG_INFO(KERNEL, "AOT: disabled by config");
            return;
        }

        memory_system *mem = sys->get_memory_system();
        if (!mem) {
            LOG_WARN(KERNEL, "AOT: memory system not available");
            return;
        }

        const int max_exports = get_max_exports();
        fprintf(stderr, "AOT: max_exports=%d\n", max_exports);

        auto targets = get_targets();
        if (targets.empty()) {
            LOG_INFO(KERNEL, "AOT: no targets configured");
            return;
        }

        // Scan ROM for target DLLs by UID3.
        // The ROM is mapped starting at 0x80000000. Scan for rom_image_headers.
        // ROM headers start with uid1=0x10000079 (DLL).
        loader::rom *romf = sys->get_rom_info();
        if (!romf) {
            LOG_WARN(KERNEL, "AOT: ROM info not available");
            return;
        }

        const std::uint32_t rom_base = romf->header.rom_base;
        const std::uint32_t rom_size = romf->header.rom_size;

        fprintf(stderr, "AOT: scanning ROM at 0x%08X (%u bytes) for %zu target DLL(s)\n",
            rom_base, rom_size, targets.size());
        LOG_INFO(KERNEL, "AOT: scanning ROM at 0x{:08X} ({} bytes) for {} target DLL(s)",
            rom_base, rom_size, targets.size());

        std::uint8_t *rom_host = reinterpret_cast<std::uint8_t *>(mem->get_real_pointer(rom_base));
        if (!rom_host) {
            LOG_ERROR(KERNEL, "AOT: cannot get ROM host pointer");
            return;
        }

        std::vector<wasm_func_def> all_funcs;

        for (std::uint32_t offset = 0; offset + sizeof(rom_image_header_raw) < rom_size; offset += 4) {
            std::uint32_t uid1;
            std::memcpy(&uid1, rom_host + offset, 4);
            if (uid1 != DLL_UID1) continue;

            rom_image_header_raw hdr;
            std::memcpy(&hdr, rom_host + offset, sizeof(hdr));

            if (hdr.code_size <= 0 || hdr.code_size > 0x1000000) continue;
            if (hdr.export_dir_count <= 0 || hdr.export_dir_count > 10000) continue;
            if (hdr.code_address < rom_base || hdr.code_address >= rom_base + rom_size) continue;

            // Check if this matches any target
            const aot_target *target = nullptr;
            for (const auto &t : targets) {
                if (t.uid3 == hdr.uid3) {
                    target = &t;
                    break;
                }
            }
            if (!target) continue;

            LOG_INFO(KERNEL, "AOT: found {} (UID3=0x{:08X}) at ROM+0x{:X}, code=0x{:08X}, {} exports",
                target->display_name, hdr.uid3, offset, hdr.code_address, hdr.export_dir_count);

            // Read export table from ROM
            if (hdr.export_dir_address < rom_base || hdr.export_dir_address >= rom_base + rom_size) {
                LOG_WARN(KERNEL, "AOT: export dir address 0x{:08X} outside ROM", hdr.export_dir_address);
                continue;
            }

            std::uint8_t *export_table_host = rom_host + (hdr.export_dir_address - rom_base);
            std::uint8_t *code_host = rom_host + (hdr.code_address - rom_base);

            // Determine which ordinals to translate
            std::vector<std::uint32_t> ordinals_to_translate;
            if (target->ordinals.empty()) {
                for (int i = 1; i <= hdr.export_dir_count; i++) {
                    ordinals_to_translate.push_back(i);
                }
            } else {
                ordinals_to_translate = target->ordinals;
            }

            int arm_count = 0, dup_count = 0, zero_count = 0, oob_count = 0;
            std::set<std::uint32_t> translated_addrs;

            // Collect candidate functions: (ordinal, func_addr, func_host, func_size).
            struct candidate {
                std::uint32_t ordinal;
                std::uint32_t func_addr;
                std::uint8_t *func_host;
                std::uint32_t func_size;
            };
            std::vector<candidate> candidates;

            for (std::uint32_t ordinal : ordinals_to_translate) {
                if (ordinal < 1 || ordinal > static_cast<std::uint32_t>(hdr.export_dir_count)) continue;

                std::uint32_t export_addr;
                std::memcpy(&export_addr, export_table_host + (ordinal - 1) * 4, 4);
                if (export_addr == 0) { zero_count++; continue; }

                bool is_thumb = (export_addr & 1) != 0;
                std::uint32_t func_addr = export_addr & ~1u;

                if (func_addr < hdr.code_address || func_addr >= hdr.code_address + hdr.code_size) { oob_count++; continue; }
                if (!is_thumb) { arm_count++; continue; } // translator only handles Thumb for now
                if (translated_addrs.count(func_addr)) { dup_count++; continue; } // skip duplicate exports
                translated_addrs.insert(func_addr);

                std::uint32_t func_offset = func_addr - hdr.code_address;
                std::uint8_t *func_host = code_host + func_offset;

                // Determine function size: scan to next export or end of code
                std::uint32_t max_size = hdr.code_size - func_offset;
                if (max_size > 4096) max_size = 4096; // cap
                std::uint32_t func_size = std::min(max_size, 1024u);

                candidates.push_back({ordinal, func_addr, func_host, func_size});
            }

            // First pass: discover which exported candidates translate successfully,
            // building a sibling map (address → wasm func index).
            // WASM function indices start at num_imports (3) and go up.
            const std::uint32_t num_imports = 3;
            sibling_map siblings;
            struct accepted_func {
                std::uint32_t ordinal; // 0 for internal helpers
                std::uint32_t func_addr;
                std::uint8_t *func_host;
                std::uint32_t func_size;
            };
            std::vector<accepted_func> accepted;

            auto try_translate_at = [&](std::uint32_t addr, std::uint32_t ordinal) -> bool {
                // Must be within code range, aligned, and not already translated
                if (addr < hdr.code_address || addr >= hdr.code_address + hdr.code_size) return false;
                if (addr & 1) return false; // should already be masked
                if (siblings.count(addr)) return false;
                std::uint32_t offset = addr - hdr.code_address;
                std::uint8_t *host = code_host + offset;
                std::uint32_t max_size = hdr.code_size - offset;
                if (max_size > 4096) max_size = 4096;
                std::uint32_t func_size = std::min(max_size, 1024u);
                auto tr = translate_thumb_block(host, func_size, addr, nullptr);
                if (tr.func.body.empty() || !tr.complete) return false;
                std::uint32_t func_idx = num_imports + static_cast<std::uint32_t>(accepted.size());
                siblings[addr] = func_idx;
                accepted.push_back({ordinal, addr, host, func_size});
                return true;
            };

            for (const auto &c : candidates) {
                try_translate_at(c.func_addr, c.ordinal);
                if (max_exports >= 0 && static_cast<int>(accepted.size()) >= max_exports) break;
            }
            fprintf(stderr, "AOT: first pass: %zu export candidates, %zu accepted\n",
                candidates.size(), accepted.size());

            // Recursively discover internal helper functions by scanning BL
            // targets in already-accepted functions. Each BL target that's
            // within the DLL code range and translates successfully is added
            // as an internal helper.
            if (max_exports < 0) {
                size_t start_idx = 0;
                int rounds = 0;
                while (start_idx < accepted.size() && rounds++ < 10) {
                    size_t end_idx = accepted.size();
                    for (size_t i = start_idx; i < end_idx; i++) {
                        const auto &f = accepted[i];
                        // Scan the code for BL instructions and try to translate targets
                        for (std::uint32_t off = 0; off + 3 < f.func_size; off += 2) {
                            std::uint16_t w1 = f.func_host[off] | (f.func_host[off+1] << 8);
                            std::uint16_t w2 = f.func_host[off+2] | (f.func_host[off+3] << 8);
                            if (((w1 & 0xF800) == 0xF000) && ((w2 & 0xD000) == 0xD000)) {
                                // Decode BL target
                                std::uint32_t s = (w1 >> 10) & 1;
                                std::uint32_t imm10 = w1 & 0x3FF;
                                std::uint32_t j1 = (w2 >> 13) & 1;
                                std::uint32_t j2 = (w2 >> 11) & 1;
                                std::uint32_t imm11 = w2 & 0x7FF;
                                std::uint32_t i1 = !(j1 ^ s);
                                std::uint32_t i2 = !(j2 ^ s);
                                std::int32_t imm32 = static_cast<std::int32_t>(
                                    (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1));
                                if (s) imm32 |= 0xFF000000;
                                std::uint32_t target = f.func_addr + off + 4 + imm32;
                                try_translate_at(target & ~1u, 0);
                                // Skip second halfword
                                off += 2;
                            }
                        }
                    }
                    start_idx = end_idx;
                }
                fprintf(stderr, "AOT: after helper discovery: %zu total functions\n",
                    accepted.size());
            }

            // Second pass: re-translate with the sibling map, emitting direct
            // calls for BL targets that are other AOT functions.
            for (const auto &a : accepted) {
                auto tr = translate_thumb_block(a.func_host, a.func_size, a.func_addr, &siblings);
                if (tr.func.body.empty() || !tr.complete) {
                    fprintf(stderr, "AOT: 2nd pass failed for 0x%08X\n", a.func_addr);
                    continue;
                }
                if (a.ordinal != 0) {
                    fprintf(stderr, "AOT: [%zu] ordinal %u at 0x%08X: OK (%zu bytes)\n",
                        all_funcs.size() + 1, a.ordinal, a.func_addr, tr.func.body.size());
                }
                all_funcs.push_back(std::move(tr.func));
            }

            LOG_INFO(KERNEL, "AOT: translated {}/{} exports for {} (arm={}, dup={}, zero={}, oob={})",
                all_funcs.size(), ordinals_to_translate.size(), target->display_name,
                arm_count, dup_count, zero_count, oob_count);
            fprintf(stderr, "AOT: translated %zu/%zu exports for %s (arm=%d dup=%d zero=%d oob=%d)\n",
                all_funcs.size(), ordinals_to_translate.size(), target->display_name.c_str(),
                arm_count, dup_count, zero_count, oob_count);
        }

        if (all_funcs.empty()) {
            LOG_WARN(KERNEL, "AOT: no functions translated");
            return;
        }

        // Build and instantiate one WASM module for all functions
        std::vector<wasm_import_func> imports = {
            {"env", "tlb_read32", 2, true},
            {"env", "tlb_write32", 3, false},
            {"env", "tlb_read8", 2, true},
        };

        auto wasm_bytes = build_wasm_module(all_funcs, imports);
        LOG_INFO(KERNEL, "AOT: built WASM module ({} bytes, {} functions)",
            wasm_bytes.size(), all_funcs.size());

        // Stage for deferred instantiation on the worker thread.
        // addFunction must be called on the thread that will use the table.
        stage_aot_module(std::move(wasm_bytes), "rom-aot");
    }
}
