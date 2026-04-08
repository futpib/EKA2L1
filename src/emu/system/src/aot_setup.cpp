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
#include <kernel/kernel.h>
#include <kernel/codeseg.h>
#include <common/log.h>

namespace eka2l1::arm::aot {
    void initialize(eka2l1::system *sys, const std::string &config_override) {
        // Register all built-in AOT function implementations
        register_builtin_functions();

        // Apply configuration
        catalog &cat = global_catalog();
        std::string config_str = config_override.empty() ? default_config_string() : config_override;

        if (config_str == "none") {
            cat.set_config({});
            LOG_INFO(KERNEL, "AOT: disabled by config");
            return;
        }

        auto config = parse_config_string(config_str);
        cat.set_config(config);

        LOG_INFO(KERNEL, "AOT: config = \"{}\"", config_str);
        for (const auto &cfg : config) {
            if (cfg.symbols.empty()) {
                LOG_INFO(KERNEL, "AOT:   {} (all symbols)", cfg.dll_name);
            } else {
                for (const auto &sym : cfg.symbols) {
                    LOG_INFO(KERNEL, "AOT:   {}:{}", cfg.dll_name, sym);
                }
            }
        }

        // Register codeseg loaded callback to activate AOT functions at runtime
        kernel_system *kern = sys->get_kernel_system();
        if (!kern) {
            LOG_WARN(KERNEL, "AOT: kernel not available, skipping callback registration");
            return;
        }

        kern->register_codeseg_loaded_callback(
            [](const std::string &lib_name, kernel::process *proc, codeseg_ptr seg) {
                catalog &cat = global_catalog();
                registry &reg = global_registry();

                auto entries = cat.get_entries_for_dll(lib_name);
                if (entries.empty()) {
                    return;
                }

                address code_run_addr = seg->get_code_run_addr(proc, nullptr);
                cat.activate_dll(lib_name, code_run_addr, reg);

                LOG_INFO(KERNEL, "AOT: activated {} function(s) for {} at code base 0x{:08X}",
                    entries.size(), lib_name, code_run_addr);
            }
        );

        LOG_INFO(KERNEL, "AOT: initialized with {} catalog entries", cat.entries().size());
    }
}
