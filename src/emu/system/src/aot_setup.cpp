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

#include <cstring>
#include <set>

namespace eka2l1::arm::aot {
    // AOT target: DLL identified by UID3, with list of ordinals to translate.
    // Empty ordinals = translate all exports.
    // -1 = unlimited, 0..N = limit. For bisecting crashes.
    // 16 exports verified working. Export #17 (ordinal 27, addr 0x804650B6) crashes.
    static constexpr int MAX_EXPORTS = 16;

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

            std::set<std::uint32_t> translated_addrs;
            for (std::uint32_t ordinal : ordinals_to_translate) {
                if (ordinal < 1 || ordinal > static_cast<std::uint32_t>(hdr.export_dir_count)) continue;

                std::uint32_t export_addr;
                std::memcpy(&export_addr, export_table_host + (ordinal - 1) * 4, 4);
                if (export_addr == 0) continue;

                bool is_thumb = (export_addr & 1) != 0;
                std::uint32_t func_addr = export_addr & ~1u;

                if (func_addr < hdr.code_address || func_addr >= hdr.code_address + hdr.code_size) continue;
                if (!is_thumb) continue; // translator only handles Thumb for now
                if (translated_addrs.count(func_addr)) continue; // skip duplicate exports
                translated_addrs.insert(func_addr);

                std::uint32_t func_offset = func_addr - hdr.code_address;
                std::uint8_t *func_host = code_host + func_offset;

                // Determine function size: scan to next export or end of code
                std::uint32_t max_size = hdr.code_size - func_offset;
                if (max_size > 4096) max_size = 4096; // cap

                // Find a tighter bound by looking for PUSH as next function start
                // or cap at 256 bytes for safety
                std::uint32_t func_size = std::min(max_size, 256u);

                auto tr = translate_thumb_block(func_host, func_size, func_addr);
                static int skip_log = 0;
                if (tr.func.body.empty()) {
                    if (skip_log++ < 5)
                        fprintf(stderr, "AOT: ordinal %u at 0x%08X: empty body\n", ordinal, func_addr);
                    continue;
                }
                if (!tr.complete) {
                    if (skip_log++ < 5)
                        fprintf(stderr, "AOT: ordinal %u at 0x%08X: incomplete\n", ordinal, func_addr);
                    continue;
                }

                all_funcs.push_back(std::move(tr.func));
                if (MAX_EXPORTS >= 0 && static_cast<int>(all_funcs.size()) >= MAX_EXPORTS) break;
            }

            LOG_INFO(KERNEL, "AOT: translated {}/{} exports for {}",
                all_funcs.size(), ordinals_to_translate.size(), target->display_name);
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

        int count = instantiate_aot_module(wasm_bytes, "rom-aot");
        LOG_INFO(KERNEL, "AOT: {} functions registered", count);
    }
}
