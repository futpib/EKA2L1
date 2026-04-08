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
#include <common/log.h>

namespace eka2l1::arm::aot {
    void initialize(eka2l1::system *sys, const std::string &config_override) {
        if (config_override == "none") {
            LOG_INFO(KERNEL, "AOT: disabled by config");
            return;
        }

        // TODO: AOT translation by DLL name + ordinal.
        // The translator, WASM emitter, and runtime glue are ready.
        // What's needed:
        // 1. Hook into ROM DLL loading (FntStore.dll doesn't go through
        //    lib_manager::load, so the codeseg callback doesn't fire for it)
        // 2. Configure target DLLs by name + ordinal list
        // 3. At load time: look up ordinal -> address, read code bytes,
        //    translate, build WASM module, instantiate, register
        LOG_INFO(KERNEL, "AOT: initialized (translation not yet configured)");
    }
}
