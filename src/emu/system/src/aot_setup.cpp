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
#include <cpu/aot/state_locals.h>
#include <cpu/aot/arm_translator.h>
#include <cpu/aot/memory_experiment.h>
#include <cpu/aot/thumb_translator.h>
#include <cpu/aot/wasm_emitter.h>
#include <kernel/kernel.h>
#include <mem/mem.h>
#include <loader/rom.h>
#include <common/log.h>
#include <common/deterministic.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>

namespace eka2l1::arm::aot {
    // AOT target: DLL identified by UID3, with list of ordinals to translate.
    // Empty ordinals = translate all exports.
    // Override with EKA2L1_AOT_MAX_EXPORTS env var (-1 = unlimited, 0..N = limit).
    static int get_max_exports() {
        const char *env = std::getenv("EKA2L1_AOT_MAX_EXPORTS");
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
            // euser.dll — exports are ARM mode, handled by ARM translator
            { 0x100039E5, "euser.dll", {} },
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
        // Establish the shared interpreter baseline before comparing AOT.
        if (common::benchmark::enabled() && !std::getenv("EKA2L1_BENCHMARK_AOT")) return;
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

        const char *hot_env = std::getenv("EKA2L1_AOT_HOT");
        configure_hot_rom(rom_host, rom_base, rom_size, hot_env && hot_env[0] == '1');
        const auto eager_start = std::chrono::steady_clock::now();
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
            if (static_cast<std::uint32_t>(hdr.code_size) > rom_size - (hdr.code_address - rom_base)) continue;

            // Register this DLL in the module map for instruction tracking.
            if (hdr.uid3 != 0) {
                char uid_name[32];
                snprintf(uid_name, sizeof(uid_name), "ROM:0x%08X", hdr.uid3);
                register_module(hdr.code_address,
                    static_cast<std::uint32_t>(hdr.code_size), uid_name);
            }

            // Check if this matches any target
            const aot_target *target = nullptr;
            for (const auto &t : targets) {
                if (t.uid3 == hdr.uid3) {
                    target = &t;
                    break;
                }
            }
            if (!target) continue;

            // Validate: export dir should be near the code section (within
            // code_address ± 2*code_size). False positive ROM header scans
            // produce wildly inconsistent addresses.
            {
                std::int64_t dir_dist = static_cast<std::int64_t>(hdr.export_dir_address)
                    - static_cast<std::int64_t>(hdr.code_address);
                if (dir_dist < 0) dir_dist = -dir_dist;
                if (dir_dist > 2 * hdr.code_size) {
                    continue; // export dir too far from code — false positive
                }
            }

            // Re-register with proper name
            register_module(hdr.code_address,
                static_cast<std::uint32_t>(hdr.code_size), target->display_name);

            LOG_INFO(KERNEL, "AOT: found {} (UID3=0x{:08X}) at ROM+0x{:X}, code=0x{:08X}, {} exports",
                target->display_name, hdr.uid3, offset, hdr.code_address, hdr.export_dir_count);

            // Read export table from ROM
            if (hdr.export_dir_address < rom_base || hdr.export_dir_address >= rom_base + rom_size) {
                LOG_WARN(KERNEL, "AOT: export dir address 0x{:08X} outside ROM", hdr.export_dir_address);
                continue;
            }

            if (static_cast<std::uint32_t>(hdr.export_dir_count) > (rom_size - (hdr.export_dir_address - rom_base)) / 4) continue;
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

            // Collect candidate functions: (ordinal, func_addr, func_host, func_size, is_arm).
            struct candidate {
                std::uint32_t ordinal;
                std::uint32_t func_addr;
                std::uint8_t *func_host;
                std::uint32_t func_size;
                bool is_arm;
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
                if (!is_thumb) {
                    arm_count++;
                    // ARM-mode: word-align address
                    func_addr &= ~3u;
                }
                if (translated_addrs.count(func_addr)) { dup_count++; continue; }
                translated_addrs.insert(func_addr);

                std::uint32_t func_offset = func_addr - hdr.code_address;
                std::uint8_t *func_host = code_host + func_offset;

                // Determine function size: scan to next export or end of code
                std::uint32_t max_size = hdr.code_size - func_offset;
                if (max_size > 4096) max_size = 4096; // cap
                std::uint32_t func_size = std::min(max_size, 4096u);

                candidates.push_back({ordinal, func_addr, func_host, func_size, !is_thumb});
            }

            // Compile bounded, mode-tagged blocks. Branches return to dispatch,
            // avoiding recursive sibling calls and cross-DLL function-index aliases.
            std::set<std::uint32_t> visited;
            std::vector<candidate> pending = candidates;
            std::size_t accepted = 0;
            for (std::size_t i = 0; i < pending.size() && i < 16384; ++i) {
                const auto c = pending[i];
                const auto key = c.func_addr | (c.is_arm ? 0u : 1u);
                if (!visited.insert(key).second) continue;
                const auto size = std::min(c.func_size, chaining_enabled ? 512u : 128u);
                auto translate=[&]{return c.is_arm
                    ? translate_arm_block(c.func_host, size, c.func_addr, nullptr, nullptr, true, false, chaining_enabled)
                    : translate_thumb_block(c.func_host, size, c.func_addr, nullptr, nullptr, true, false, chaining_enabled);};
                auto tr=translate();
                if (tr.func.body.empty() || !tr.entry_supported) continue;
                tr.func.export_name = "f_" + std::to_string(key);
                if(memory_experiment::mode==2 && !memory_experiment::identity_active) {
                    memory_experiment::mode=0;auto warmup=translate();memory_experiment::mode=2;
                    if(warmup.func.body.empty() || !warmup.entry_supported) std::abort();
                    warmup.func.export_name=tr.func.export_name+"__warmup";all_funcs.push_back(std::move(warmup.func));
                }
                all_funcs.push_back(std::move(tr.func));
                ++accepted;
                if (max_exports >= 0 && accepted >= static_cast<std::size_t>(max_exports)) break;
                // Resume and local branch entries keep the caller's instruction mode.
                tr.branch_targets.insert(tr.branch_targets.end(), tr.resume_points.begin(), tr.resume_points.end());
                tr.branch_targets.push_back(tr.end_address);
                for (auto addr : tr.branch_targets) {
                    if (addr < hdr.code_address || addr >= hdr.code_address + hdr.code_size) continue;
                    if (addr & (c.is_arm ? 3u : 1u)) continue;
                    const auto off = addr - hdr.code_address;
                    pending.push_back({0, addr, code_host + off,
                        static_cast<std::uint32_t>(hdr.code_size) - off, c.is_arm});
                }
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
            {"env", "tlb_write8", 3, false},
            {"env", "tlb_read16", 2, true},
            {"env", "tlb_write16", 3, false},
        };

        if (arm_exclusive_memory) imports.push_back({"env", "arm_exclusive", 2, false});

        const auto emission_start = std::chrono::steady_clock::now();
        auto wasm_bytes = build_wasm_module(all_funcs, imports);
        fprintf(stderr, "AOT: scan_translate_ms=%.3f emit_ms=%.3f bytes=%zu functions=%zu\n",
            std::chrono::duration<double, std::milli>(emission_start - eager_start).count(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - emission_start).count(),
            wasm_bytes.size(), all_funcs.size());
        LOG_INFO(KERNEL, "AOT: built WASM module ({} bytes, {} functions)",
            wasm_bytes.size(), all_funcs.size());

        // Stage for deferred instantiation on the worker thread.
        // addFunction must be called on the thread that will use the table.
        stage_aot_module(std::move(wasm_bytes), "rom-aot");
    }
}
