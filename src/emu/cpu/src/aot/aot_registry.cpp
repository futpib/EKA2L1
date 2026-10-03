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
#include <algorithm>
#include <cstdio>

namespace eka2l1::arm::aot {
    // --- registry ---

    void registry::register_function(std::uint32_t arm_address, aot_func func) {
        functions_[arm_address] = func;
        recent_[recent_index(arm_address)] = {arm_address, func};
    }

    void registry::unregister_function(std::uint32_t arm_address) {
        functions_.erase(arm_address);
        auto &slot = recent_[recent_index(arm_address)];
        if (slot.address == arm_address) slot = {};
    }

    void registry::clear() {
        functions_.clear();
        recent_.fill({});
    }

    aot_func registry::lookup_uncached(std::uint32_t arm_address, recent_function &slot) const {
        auto it = functions_.find(arm_address);
        if (it != functions_.end()) {
            slot = {arm_address, it->second};
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

    // --- Module map ---
    static std::vector<module_range> g_modules;

    void register_module(std::uint32_t base, std::uint32_t size, const std::string &name) {
        // Update name if already registered
        for (auto &m : g_modules) {
            if (m.base == base) { m.name = name; return; }
        }
        g_modules.push_back({base, base + size, name, 0, 0});
        // Keep sorted by base for binary search
        std::sort(g_modules.begin(), g_modules.end(),
            [](const module_range &a, const module_range &b) { return a.base < b.base; });
        // Suppress per-module log — too many ROM DLLs
    }

    module_range *lookup_module(std::uint32_t pc) {
        if (g_modules.empty()) return nullptr;
        // Binary search: find last module with base <= pc
        auto it = std::upper_bound(g_modules.begin(), g_modules.end(), pc,
            [](std::uint32_t addr, const module_range &m) { return addr < m.base; });
        if (it == g_modules.begin()) return nullptr;
        --it;
        if (pc < it->end) return &(*it);
        return nullptr;
    }

    void dump_module_stats() {
        fprintf(stderr, "\n=== Per-module instruction stats ===\n");
        for (auto &m : g_modules) {
            if (m.aot_instrs == 0 && m.interp_dispatches == 0) continue;
            double pct = (m.aot_instrs + m.interp_dispatches) > 0
                ? 100.0 * m.aot_instrs / (m.aot_instrs + m.interp_dispatches) : 0;
            fprintf(stderr, "  %-30s AOT: %12llu  interp: %12llu  (%.1f%% AOT)\n",
                m.name.c_str(),
                (unsigned long long)m.aot_instrs,
                (unsigned long long)m.interp_dispatches,
                pct);
        }
        fprintf(stderr, "====================================\n\n");
    }
}
