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

#include <cpu/aot/aot_registry.h>
#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/thumb_translator.h>
#include <cpu/aot/wasm_emitter.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/arm_interface.h>
#include <algorithm>
#include <cstdio>
#include <set>

namespace eka2l1::arm::aot {
    // --- registry ---

    void registry::register_function(std::uint32_t arm_address, aot_func func) {
        functions_[arm_address] = func;
    }

    void registry::unregister_function(std::uint32_t arm_address) {
        functions_.erase(arm_address);
    }

    void registry::clear() {
        functions_.clear();
    }

    aot_func registry::lookup(std::uint32_t arm_address) const {
        auto it = functions_.find(arm_address);
        if (it != functions_.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool registry::has_function(std::uint32_t arm_address) const {
        return functions_.find(arm_address) != functions_.end();
    }

    std::size_t registry::size() const {
        return functions_.size();
    }

    // --- catalog ---

    void catalog::add_entry(const catalog_entry &entry) {
        entries_.push_back(entry);
    }

    void catalog::set_config(const std::vector<dll_config> &config) {
        config_ = config;
    }

    const std::vector<dll_config> &catalog::get_config() const {
        return config_;
    }

    static bool iequals(const std::string &a, const std::string &b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); i++) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    bool catalog::is_enabled(const std::string &dll_name, const std::string &symbol_name) const {
        for (const auto &cfg : config_) {
            if (iequals(cfg.dll_name, dll_name)) {
                if (cfg.symbols.empty()) {
                    return true; // whole DLL enabled
                }
                for (const auto &sym : cfg.symbols) {
                    if (iequals(sym, symbol_name)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    std::vector<const catalog_entry *> catalog::get_entries_for_dll(const std::string &dll_name) const {
        std::vector<const catalog_entry *> result;
        for (const auto &entry : entries_) {
            if (iequals(entry.dll_name, dll_name) && is_enabled(entry.dll_name, entry.symbol_name)) {
                result.push_back(&entry);
            }
        }
        return result;
    }

    void catalog::activate_dll(const std::string &dll_name, std::uint32_t code_base, registry &reg) const {
        for (const auto &entry : entries_) {
            if (iequals(entry.dll_name, dll_name) && is_enabled(entry.dll_name, entry.symbol_name)) {
                reg.register_function(code_base + entry.code_offset, entry.func);
            }
        }
    }

    // --- globals ---

    registry &global_registry() {
        static registry instance;
        return instance;
    }

    catalog &global_catalog() {
        static catalog instance;
        return instance;
    }

    // --- dispatch history ---
    dispatch_record history[AOT_HISTORY] = {};
    std::size_t history_head = 0;

    void dump_history() {
        fprintf(stderr, "AOT dispatch history (oldest first; ring buffer of %zu):\n",
            AOT_HISTORY);
        for (std::size_t k = 0; k < AOT_HISTORY; k++) {
            std::size_t idx = (history_head + k) % AOT_HISTORY;
            const auto &r = history[idx];
            if (!r.entry_pc) continue;
            fprintf(stderr,
                "  [%zu] entry=0x%08X exit=0x%08X instrs=%u\n",
                k, r.entry_pc, r.exit_pc, r.instrs);
            fprintf(stderr, "      before:");
            for (int i = 0; i < 16; i++) fprintf(stderr, " r%d=0x%08X", i, r.regs_before[i]);
            fprintf(stderr, "\n      after: ");
            for (int i = 0; i < 16; i++) fprintf(stderr, " r%d=0x%08X", i, r.regs_after[i]);
            fprintf(stderr, "\n");
        }
    }

    // --- config parsing ---

    std::vector<dll_config> parse_config_string(const std::string &config_str) {
        std::vector<dll_config> result;
        if (config_str.empty()) return result;

        // Split by comma
        std::size_t start = 0;
        while (start < config_str.size()) {
            std::size_t comma = config_str.find(',', start);
            if (comma == std::string::npos) comma = config_str.size();

            std::string spec = config_str.substr(start, comma - start);
            start = comma + 1;

            if (spec.empty()) continue;

            dll_config cfg;

            // Split by colon: first part is dll_name, rest are symbols
            std::size_t colon_start = 0;
            std::size_t colon = spec.find(':', colon_start);
            if (colon == std::string::npos) {
                cfg.dll_name = spec;
            } else {
                cfg.dll_name = spec.substr(0, colon);
                colon_start = colon + 1;
                while (colon_start < spec.size()) {
                    colon = spec.find(':', colon_start);
                    if (colon == std::string::npos) colon = spec.size();
                    std::string sym = spec.substr(colon_start, colon - colon_start);
                    if (!sym.empty()) {
                        cfg.symbols.push_back(sym);
                    }
                    colon_start = colon + 1;
                }
            }

            result.push_back(cfg);
        }

        return result;
    }

    // --- builtins ---

    const char *default_config_string() {
        // Default: AOT-compile all registered functions in gdi.dll
        return "gdi.dll";
    }

    void register_builtin_functions() {
    }

    // --- profile-guided translation ---

    void try_translate_hot_pcs(ARMul_State *cpu,
        const std::map<std::uint32_t, std::uint64_t> &histogram)
    {
        if (histogram.empty()) return;

        registry &reg = global_registry();

        // Sort PCs by hotness
        std::vector<std::pair<std::uint32_t, std::uint64_t>> sorted(histogram.begin(), histogram.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

        // Find the hottest basic block that we haven't already translated.
        // Group consecutive hot PCs into a block.
        std::vector<wasm_func_def> funcs;
        std::set<std::uint32_t> translated_starts;

        for (const auto &[pc, count] : sorted) {
            // Skip if already translated or if it's in the dispatcher trampoline area
            if (reg.has_function(pc)) continue;
            if (pc < 0x80000000) continue; // skip non-ROM addresses
            if (count < 100) break; // stop at low-count PCs

            // Find the start of this basic block by scanning backwards for PUSH or a branch target
            // For simplicity, just use the hot PC as the start of a small block
            std::uint32_t block_start = pc & ~1; // align
            if (translated_starts.count(block_start)) continue;

            // Read code from host memory. The PC is a virtual address in the emulated
            // ARM address space. We need to resolve it to a host pointer.
            std::uint32_t code_word = 0;
            if (!cpu->parent()->read_code(block_start, &code_word)) {
                continue; // can't read code at this address
            }

            // Read a block of code around this PC.
            // Scan forward to find the block end (branch, return, or max size).
            const std::uint32_t MAX_BLOCK = 128;
            std::vector<std::uint8_t> code_bytes;
            for (std::uint32_t off = 0; off < MAX_BLOCK; off += 2) {
                std::uint32_t addr = block_start + off;
                std::uint32_t word = 0;
                if (!cpu->parent()->read_code(addr & ~3, &word)) break;
                // Extract the halfword
                std::uint16_t hw;
                if (addr & 2) {
                    hw = static_cast<std::uint16_t>(word >> 16);
                } else {
                    hw = static_cast<std::uint16_t>(word & 0xFFFF);
                }
                code_bytes.push_back(hw & 0xFF);
                code_bytes.push_back((hw >> 8) & 0xFF);

                // Check for block-ending instructions
                // POP {.., PC} or BX LR
                if ((hw & 0xFF00) == 0xBD00) break; // POP with PC
                if (hw == 0x4770) break; // BX LR
                // Unconditional B to outside the block — if it goes backwards far, stop
                if ((hw & 0xF800) == 0xE000) {
                    std::int16_t boff = static_cast<std::int16_t>((hw & 0x7FF) << 5) >> 5;
                    std::uint32_t target = addr + 4 + boff * 2;
                    if (target < block_start || target >= block_start + MAX_BLOCK) {
                        code_bytes.push_back(0); code_bytes.push_back(0); // pad
                        break;
                    }
                }
            }

            if (code_bytes.size() < 4) continue;

            auto tr = translate_thumb_block(code_bytes.data(), code_bytes.size(), block_start);
            if (tr.func.body.empty()) continue;
            auto &func = tr.func;

            translated_starts.insert(block_start);
            fprintf(stderr, "AOT: translated hot block at 0x%08X (%zu bytes, %llu samples)\n",
                block_start, code_bytes.size(), (unsigned long long)count);
            funcs.push_back(std::move(func));

            if (funcs.size() >= 5) break; // limit per batch
        }

        if (funcs.empty()) return;

        // Build and instantiate WASM module
        std::vector<wasm_import_func> imports = {
            {"env", "tlb_read32", 2, true},
            {"env", "tlb_write32", 3, false},
            {"env", "tlb_read8", 2, true},
            {"env", "tlb_write8", 3, false},
        };

        auto wasm_bytes = build_wasm_module(funcs, imports);
        fprintf(stderr, "AOT: built WASM module (%zu bytes, %zu functions)\n",
            wasm_bytes.size(), funcs.size());

        stage_aot_module(std::move(wasm_bytes), "profile-guided");
    }
}
