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
        // Placeholder — AOT function implementations will be added here
        // as they are developed. Each call adds a catalog_entry to global_catalog().
    }
}
