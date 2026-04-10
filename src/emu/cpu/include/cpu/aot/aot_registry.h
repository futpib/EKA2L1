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

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct ARMul_State;

namespace eka2l1::arm::aot {
    // An AOT function takes the CPU state, executes the equivalent of the ARM code,
    // and returns the number of ARM instructions it replaced (for tick accounting).
    using aot_func = std::uint32_t (*)(ARMul_State *cpu);

    // A catalog entry describes an AOT-compiled function and where it comes from.
    struct catalog_entry {
        std::string dll_name;
        std::string symbol_name;
        std::uint32_t code_offset;  // Offset from DLL code base
        aot_func func;
    };

    // Specifies which DLLs/symbols should use AOT.
    // If symbols is empty, the entire DLL is opted in.
    struct dll_config {
        std::string dll_name;
        std::vector<std::string> symbols; // empty = all symbols in this DLL
    };

    // The runtime registry maps absolute ARM addresses to AOT functions.
    // Populated when DLLs are loaded at known addresses.
    class registry {
    private:
        std::unordered_map<std::uint32_t, aot_func> functions_;

    public:
        void register_function(std::uint32_t arm_address, aot_func func);
        void unregister_function(std::uint32_t arm_address);
        void clear();

        aot_func lookup(std::uint32_t arm_address) const;
        bool has_function(std::uint32_t arm_address) const;
        std::size_t size() const;
    };

    // The catalog holds all available AOT translations, indexed by DLL.
    class catalog {
    private:
        std::vector<catalog_entry> entries_;
        std::vector<dll_config> config_;

    public:
        void add_entry(const catalog_entry &entry);
        void set_config(const std::vector<dll_config> &config);
        const std::vector<dll_config> &get_config() const;

        // Check if a given DLL/symbol is enabled in the config.
        bool is_enabled(const std::string &dll_name, const std::string &symbol_name) const;

        // Get all entries for a given DLL that are enabled by config.
        std::vector<const catalog_entry *> get_entries_for_dll(const std::string &dll_name) const;

        // Register enabled entries into a registry, given a DLL's runtime code base address.
        void activate_dll(const std::string &dll_name, std::uint32_t code_base, registry &reg) const;

        const std::vector<catalog_entry> &entries() const { return entries_; }
    };

    // Global instances.
    registry &global_registry();
    catalog &global_catalog();

    // Parse a config string like "gdi.dll,fbserv.dll:symbol1:symbol2"
    // Format: comma-separated DLL specs, each is "dll_name" or "dll_name:sym1:sym2"
    std::vector<dll_config> parse_config_string(const std::string &config_str);

    // Register all built-in AOT functions into the global catalog.
    void register_builtin_functions();

    // Default config string for hardcoded AOT entries.
    const char *default_config_string();

    // Profile-guided AOT: given a PC histogram, find the hottest PCs,
    // read their code from memory, translate to WASM, and register.
    void try_translate_hot_pcs(ARMul_State *cpu,
        const std::map<std::uint32_t, std::uint64_t> &histogram);

    // --- Module map for per-DLL instruction tracking ---
    struct module_range {
        std::uint32_t base;
        std::uint32_t end;  // exclusive
        std::string name;
        std::uint64_t aot_instrs = 0;
        std::uint64_t interp_dispatches = 0;
    };

    void register_module(std::uint32_t base, std::uint32_t size, const std::string &name);
    module_range *lookup_module(std::uint32_t pc);
    void dump_module_stats();

    // --- Dispatch history for crash post-mortem ---
    // The dyncom dispatch loop appends a record to `history` on every AOT
    // call. On a crash (access violation, undefined instruction, etc.) the
    // kernel exception handler calls dump_history() to print the ring
    // buffer — the entry PC plus pre/post register state — so failures
    // post-AOT can be reproduced as unit tests.
    struct dispatch_record {
        std::uint32_t entry_pc;
        std::uint32_t exit_pc;
        std::uint32_t instrs;
        std::uint32_t regs_before[16];
        std::uint32_t regs_after[16];
    };
    static constexpr std::size_t AOT_HISTORY = 16;
    extern dispatch_record history[AOT_HISTORY];
    extern std::size_t history_head;
    void dump_history();
}
