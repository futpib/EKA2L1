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

#include <string>

namespace eka2l1 {
    class system;
}

namespace eka2l1::arm::aot {
    // Initialize the AOT system: register builtin functions, apply config,
    // and hook into the kernel's codeseg-loaded callback to activate AOT
    // functions when DLLs are loaded at runtime.
    //
    // config_override: if non-empty, overrides the default AOT config.
    //   Format: "gdi.dll,fbserv.dll:symbol1:symbol2"
    //   "none" disables AOT entirely.
    void initialize(eka2l1::system *sys, const std::string &config_override = "");
}
