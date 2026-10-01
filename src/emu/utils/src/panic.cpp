/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
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

#include <common/buffer.h>

#include <utils/panic.h>
#include <yaml-cpp/yaml.h>

#include <mutex>

namespace eka2l1::epoc {
    YAML::Node panic_node;
    std::mutex panic_mut;

    bool init_panic_descriptions() {
        std::lock_guard<std::mutex> guard(panic_mut);

        panic_node.reset();

        try {
            common::ro_std_file_stream panic_json_stream("panic.json", true);
            if (!panic_json_stream.valid()) {
                return false;
            }

            std::string whole_config(panic_json_stream.size(), ' ');
            panic_json_stream.read(whole_config.data(), whole_config.size());

            panic_node = YAML::Load(whole_config)["Panic"];
        } catch (...) {
            return false;
        }

        return true;
    }

    bool is_panic_category_action_default(const std::string &panic_category) {
        std::lock_guard<std::mutex> guard(panic_mut);
        const YAML::Node &root = panic_node;
        if (!root.IsMap()) {
            return true;
        }
        const YAML::Node category = root[panic_category];
        if (!category || !category.IsMap()) {
            return true;
        }
        const YAML::Node action = category["action"];
        return !action || !action.IsScalar() || action.Scalar() != "script";
    }

    std::optional<std::string> get_panic_description(const std::string &category, const int code) {
        std::lock_guard<std::mutex> guard(panic_mut);
        const YAML::Node &root = panic_node;
        // Missing descriptions are normal, including when panic.json is absent.
        // Do not throw to detect them: WASM builds do not enable C++ catches.
        if (!root.IsMap()) {
            return std::nullopt;
        }
        const YAML::Node entries = root[category];
        if (!entries || !entries.IsMap()) {
            return std::nullopt;
        }
        const YAML::Node description = entries[code];
        if (!description || !description.IsScalar()) {
            return std::nullopt;
        }
        return description.Scalar();
    }
}
