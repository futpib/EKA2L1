/*
 * Copyright (c) 2026 EKA2L1 Team.
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

#include <drivers/graphics/context.h>

#include <emscripten/html5_webgl.h>

namespace eka2l1::drivers::graphics {
    class gl_context_emscripten : public gl_context {
    public:
        explicit gl_context_emscripten(const window_system_info &wsi, bool stereo, bool core);
        ~gl_context_emscripten() override;

        bool make_current() override;
        bool clear_current() override;

        void swap_buffers() override;
        void update(const std::uint32_t new_width, const std::uint32_t new_height) override;
        void set_swap_interval(const std::int32_t interval) override;

        bool is_headless() const override;
        std::unique_ptr<gl_context> create_shared_context() override;

    private:
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE webgl_context_;
        const char *canvas_selector_;
    };
}
