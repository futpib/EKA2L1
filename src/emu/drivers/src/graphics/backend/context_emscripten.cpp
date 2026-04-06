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

#include "context_emscripten.h"
#include <common/log.h>

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

namespace eka2l1::drivers::graphics {
    gl_context_emscripten::gl_context_emscripten(const window_system_info &wsi, bool stereo, bool core)
        : webgl_context_(0)
        , canvas_selector_("#canvas") {
        m_opengl_mode = mode::opengl_es;

        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;
        attrs.alpha = false;
        attrs.depth = true;
        attrs.stencil = true;
        attrs.antialias = false;
        attrs.preserveDrawingBuffer = true;
        attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

        webgl_context_ = emscripten_webgl_create_context(canvas_selector_, &attrs);
        if (webgl_context_ <= 0) {
            LOG_CRITICAL(DRIVER_GRAPHICS, "Failed to create WebGL2 context (error={})", webgl_context_);
            return;
        }

        if (emscripten_webgl_make_context_current(webgl_context_) != EMSCRIPTEN_RESULT_SUCCESS) {
            LOG_CRITICAL(DRIVER_GRAPHICS, "Failed to make WebGL2 context current");
            return;
        }

        int width = 0, height = 0;
        emscripten_get_canvas_element_size(canvas_selector_, &width, &height);
        m_backbuffer_width = static_cast<std::uint32_t>(width);
        m_backbuffer_height = static_cast<std::uint32_t>(height);

        LOG_INFO(DRIVER_GRAPHICS, "WebGL2 context created ({}x{})", m_backbuffer_width, m_backbuffer_height);
    }

    gl_context_emscripten::~gl_context_emscripten() {
        if (webgl_context_ > 0) {
            emscripten_webgl_destroy_context(webgl_context_);
        }
    }

    bool gl_context_emscripten::make_current() {
        return emscripten_webgl_make_context_current(webgl_context_) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    bool gl_context_emscripten::clear_current() {
        return emscripten_webgl_make_context_current(0) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    void gl_context_emscripten::swap_buffers() {
        emscripten_webgl_commit_frame();
        glFlush();
        // Force blit on the main browser thread where the canvas and GL context live.
        // emscripten_webgl_commit_frame already does this via proxy, but the canvas
        // compositing may need an explicit flush on the main thread.
        MAIN_THREAD_EM_ASM({
            if (typeof GL !== 'undefined' && GL.currentContext && GL.currentContext.defaultFbo) {
                GL.blitOffscreenFramebuffer(GL.currentContext);
            }
        });
    }

    void gl_context_emscripten::update(const std::uint32_t new_width, const std::uint32_t new_height) {
        m_backbuffer_width = new_width;
        m_backbuffer_height = new_height;
        emscripten_set_canvas_element_size(canvas_selector_, static_cast<int>(new_width), static_cast<int>(new_height));
    }

    void gl_context_emscripten::set_swap_interval(const std::int32_t interval) {
        // Not applicable for WebGL — browser controls vsync via requestAnimationFrame.
    }

    bool gl_context_emscripten::is_headless() const {
        return false;
    }

    std::unique_ptr<gl_context> gl_context_emscripten::create_shared_context() {
        // WebGL does not support shared contexts.
        // Return nullptr — the emulator must use a single GL context.
        LOG_WARN(DRIVER_GRAPHICS, "WebGL does not support shared GL contexts");
        return nullptr;
    }
}
