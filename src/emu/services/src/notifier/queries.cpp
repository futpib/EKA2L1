/*
 * Copyright (c) 2020 EKA2L1 Team
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

#include <services/notifier/queries.h>
#if !defined(EKA2L1_COOPERATIVE_JS)
#include <services/notifier/bluetooth.h>
#else
#include <services/notifier/plugin.h>
#include <utils/err.h>
#endif
#include <services/ui/plugins/keylocknof.h>
#include <services/ui/plugins/notenof.h>

namespace eka2l1::epoc::notifier {
#if defined(EKA2L1_COOPERATIVE_JS)
    class unavailable_bluetooth_selection final : public plugin_base {
    public:
        explicit unavailable_bluetooth_selection(kernel_system *kern) : plugin_base(kern) {}
        epoc::uid unique_id() const override { return 0x100069D1; }
        void handle(epoc::desc8 *, epoc::des8 *, epoc::notify_info &info) override {
            info.complete(epoc::error_not_supported);
        }
        void cancel() override {}
    };
#endif

    void add_builtin_plugins(kernel_system *kern, std::vector<plugin_instance> &plugins) {
#define ADD_PLUGIN(name) plugins.push_back(std::make_unique<name>(kern))
        ADD_PLUGIN(note_display_plugin);
        ADD_PLUGIN(keylock_plugin);
#if defined(EKA2L1_COOPERATIVE_JS)
        ADD_PLUGIN(unavailable_bluetooth_selection);
#else
        ADD_PLUGIN(bluetooth_device_selection_plugin);
#endif
#undef ADD_PLUGIN

        std::sort(plugins.begin(), plugins.end(), [](const plugin_instance &lhs, const plugin_instance &rhs) {
            return lhs->unique_id() < rhs->unique_id();
        });
    }
};