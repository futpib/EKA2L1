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

#include <common/cvt.h>
#include <common/frame_dumper.h>
#include <common/log.h>
#include <common/path.h>
#include <common/pystr.h>
#include <common/types.h>
#include <common/version.h>
#include <spdlog/spdlog.h>

#include <config/app_settings.h>
#include <config/config.h>

#include <drivers/audio/audio.h>
#include <drivers/graphics/graphics.h>
#include <drivers/graphics/backend/graphics_driver_shared.h>
#include <drivers/itc.h>

#include <kernel/kernel.h>
#include <package/manager.h>
#include <services/applist/applist.h>
#include <services/init.h>
#include <services/window/screen.h>
#include <services/window/window.h>
#include <utils/apacmd.h>
#include <system/devices.h>
#include <system/epoc.h>
#include <system/installation/install_device.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/console.h>
#include <GLES3/gl3.h>

#include <future>
#include <set>
#include <memory>
#include <thread>

using namespace eka2l1;

namespace {
    struct wasm_state {
        std::unique_ptr<eka2l1::system> symsys;
        std::unique_ptr<drivers::graphics_driver> graphics_driver;
        std::unique_ptr<drivers::audio_driver> audio_driver;
        std::unique_ptr<config::app_settings> app_settings;

        config::state conf;
        window_server *winserv = nullptr;
        bool running = false;
        bool system_started = false;
        int present_status = 0;
        std::size_t screen_redraw_cb_id = 0;
        std::unique_ptr<std::thread> emu_thread;
        std::unique_ptr<std::thread> gfx_thread;

        // Pixel readback buffer written by the gfx thread during display()
        std::mutex pixel_mutex;
        std::vector<uint8_t> pixel_buf;
        int pixel_w = 0;
        int pixel_h = 0;
        int pixel_distinct_colors = 0;

        // Frame dumper for test captures
        std::unique_ptr<common::frame_dumper> dumper;
    };

    wasm_state *g_state = nullptr;

    bool ensure_system_started() {
        if (!g_state || !g_state->symsys) return false;
        if (g_state->system_started) return true;

        device_manager *dvcmngr = g_state->symsys->get_device_manager();
        if (dvcmngr->total() == 0) {
            LOG_ERROR(FRONTEND_CMDLINE, "No device installed");
            return false;
        }

        g_state->symsys->startup();

        if (!g_state->symsys->set_device(g_state->conf.device)) {
            g_state->conf.device = 0;
            g_state->symsys->set_device(0);
        }

        g_state->symsys->mount(drive_c, drive_media::physical,
            add_path(g_state->conf.storage, "/drives/c/"), io_attrib_internal);
        g_state->symsys->mount(drive_d, drive_media::physical,
            add_path(g_state->conf.storage, "/drives/d/"), io_attrib_internal);
        g_state->symsys->mount(drive_e, drive_media::physical,
            add_path(g_state->conf.storage, "/drives/e/"), io_attrib_removeable);
        g_state->symsys->mount(drive_z, drive_media::rom,
            add_path(g_state->conf.storage, "/drives/z/"),
            io_attrib_internal | io_attrib_write_protected);

        g_state->symsys->initialize_user_parties();

        manager::packages *pkgmngr = g_state->symsys->get_packages();
        if (pkgmngr) {
            pkgmngr->load_registries();
            pkgmngr->migrate_legacy_registries();
        }

        g_state->system_started = true;
        return true;
    }

}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int eka2l1_init(const char *data_path) {
    log::setup_log(nullptr);
    log::toggle_console();
    spdlog::set_pattern("[%H:%M:%S.%e] %L %^%v%$");
    LOG_INFO(FRONTEND_CMDLINE, "EKA2L1 WASM v0.0.1 ({}-{})", GIT_BRANCH, GIT_COMMIT_HASH);

    g_state = new wasm_state();

    g_state->conf.deserialize();
    if (data_path && data_path[0]) {
        g_state->conf.storage = data_path;
    }

    if (log::filterings) {
        log::filterings->parse_filter_string(g_state->conf.log_filter);
    }

    g_state->app_settings = std::make_unique<config::app_settings>(&g_state->conf);

    system_create_components comp;
    comp.audio_ = nullptr;
    comp.graphics_ = nullptr;
    comp.conf_ = &g_state->conf;
    comp.settings_ = g_state->app_settings.get();

    g_state->symsys = std::make_unique<eka2l1::system>(comp);

    return 0;
}

EMSCRIPTEN_KEEPALIVE
int eka2l1_install_device(const char *rom_path, const char *rpkg_path) {
    if (!g_state || !g_state->symsys) {
        return -1;
    }

    device_install_params params;
    params.storage = g_state->conf.storage;

    if (rpkg_path && rpkg_path[0]) {
        params.method = device_install_method_dump_rpkg;
        params.rom_path = rom_path;
        params.rpkg_path = rpkg_path;
    } else {
        params.method = device_install_method_firmware;
        params.vpl_path = rom_path;
    }

    int last_pct = -1;
    auto progress_cb = [&last_pct](const std::uint64_t so_far, const std::uint64_t total) {
        if (total > 0) {
            int pct = static_cast<int>(so_far * 100 / total);
            if (pct != last_pct) {
                LOG_INFO(FRONTEND_CMDLINE, "Installing device: {}%", pct);
                last_pct = pct;
            }
        }
    };

    auto cancel_cb = []() -> bool { return false; };

    device_firmware_choose_variant_callback variant_cb =
        [](const std::vector<std::string> &list) -> int { return 0; };

    device_installation_error err = install_device(
        g_state->symsys->get_device_manager(), params, variant_cb, progress_cb, cancel_cb);

    if (err != device_installation_none) {
        LOG_ERROR(FRONTEND_CMDLINE, "Device installation failed with error {}", static_cast<int>(err));
        return static_cast<int>(err);
    }

    LOG_INFO(FRONTEND_CMDLINE, "Device installed successfully");
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int eka2l1_install_sis(const char *sis_path) {
    if (!ensure_system_started()) {
        return -1;
    }

    manager::packages *pkgmngr = g_state->symsys->get_packages();
    if (!pkgmngr) {
        return -1;
    }

    auto result = pkgmngr->install_package(common::utf8_to_ucs2(sis_path), drive_e, nullptr, nullptr);
    if (result != package::installation_result_success) {
        LOG_ERROR(FRONTEND_CMDLINE, "SIS installation failed (result={}): {}", static_cast<int>(result), sis_path);
        return -1;
    }

    LOG_INFO(FRONTEND_CMDLINE, "SIS installed: {}", sis_path);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int eka2l1_run(const char *app_name) {
    if (!ensure_system_started()) {
        return -1;
    }

    // Create graphics driver on its own thread. With PROXY_TO_PTHREAD, GL calls
    // from worker threads are properly proxied to the main browser thread.
    std::promise<bool> gfx_ready_promise;
    auto gfx_ready_future = gfx_ready_promise.get_future();

    g_state->gfx_thread = std::make_unique<std::thread>([&gfx_ready_promise]() {
        LOG_INFO(FRONTEND_CMDLINE, "Graphics driver thread started, creating context...");

        drivers::window_system_info wsi;
        wsi.type = drivers::window_system_type::emscripten;

        g_state->graphics_driver = drivers::create_graphics_driver(
            drivers::graphic_api::opengl, wsi);

        if (!g_state->graphics_driver) {
            LOG_ERROR(FRONTEND_CMDLINE, "Failed to create graphics driver");
            gfx_ready_promise.set_value(false);
            return;
        }

        g_state->symsys->set_graphics_driver(g_state->graphics_driver.get());
        g_state->graphics_driver->set_display_hook([]() {
            if (!g_state || !g_state->winserv) return;

            auto *scr = g_state->winserv->get_screens();
            if (!scr || !scr->screen_texture) return;

            auto &mode = scr->current_mode();
            int w = mode.size.x, h = mode.size.y;
            if (w <= 0 || h <= 0) return;

            // Read from screen_texture's FBO directly. Reading from FBO 0 on
            // WebGL2 with pthreads/OffscreenCanvas is unreliable.
            int total = w * h * 4;
            std::vector<GLubyte> pixels(total);
            auto *shared_drv = static_cast<drivers::shared_graphics_driver*>(
                g_state->graphics_driver.get());
            if (!shared_drv->read_bitmap_pixels(scr->screen_texture, w, h, pixels.data())) {
                return;
            }

            // Count distinct colors (for e2e test verification)
            std::set<uint32_t> colors;
            for (int i = 0; i < total; i += 40) {
                colors.insert((pixels[i] << 16) | (pixels[i+1] << 8) | pixels[i+2]);
            }
            {
                const std::lock_guard<std::mutex> guard(g_state->pixel_mutex);
                g_state->pixel_distinct_colors = static_cast<int>(colors.size());
            }

            // Feed frame dumper if active
            if (g_state->dumper && !g_state->dumper->done()) {
                // screen_texture FBO content is top-down (no Y-flip in projection),
                // but the frame dumper expects bottom-up GL data and flips it.
                // Pre-flip here so the double-flip produces correct output.
                int stride = w * 4;
                std::vector<GLubyte> row(stride);
                for (int y = 0; y < h / 2; y++) {
                    std::memcpy(row.data(), &pixels[y * stride], stride);
                    std::memcpy(&pixels[y * stride], &pixels[(h - 1 - y) * stride], stride);
                    std::memcpy(&pixels[(h - 1 - y) * stride], row.data(), stride);
                }
                g_state->dumper->on_frame(pixels.data(), w, h);
            }
        });
        LOG_INFO(FRONTEND_CMDLINE, "Graphics driver ready, entering command loop");
        gfx_ready_promise.set_value(true);

        g_state->graphics_driver->run();
        LOG_INFO(FRONTEND_CMDLINE, "Graphics driver thread exited");
    });

    if (!gfx_ready_future.get()) {
        LOG_ERROR(FRONTEND_CMDLINE, "Graphics driver initialization failed");
        return -1;
    }

    // Audio driver not available in WASM (Cubeb requires native audio APIs)
    LOG_INFO(FRONTEND_CMDLINE, "Skipping audio driver (not available in WASM)");

    // Launch the app via applist server (same as Qt frontend)
    LOG_INFO(FRONTEND_CMDLINE, "Launching: {}", app_name);
    std::string app_name_str(app_name);

    applist_server *svr = reinterpret_cast<applist_server *>(
        g_state->symsys->get_kernel_system()->get_by_name<service::server>("!AppListServer"));

    if (!svr) {
        LOG_ERROR(FRONTEND_CMDLINE, "Can't get app list server");
        return -2;
    }

    // Try UID first (0x...)
    if (app_name_str.length() > 2 && app_name_str.substr(0, 2) == "0x") {
        const std::uint32_t uid = common::pystr(app_name_str).as_int<std::uint32_t>();
        apa_app_registry *registry = svr->get_registration(uid);
        if (!registry) {
            LOG_ERROR(FRONTEND_CMDLINE, "No app found with UID: {}", app_name_str);
            return -2;
        }
        epoc::apa::command_line cmdline;
        cmdline.launch_cmd_ = epoc::apa::command_create;
        if (!svr->launch_app(*registry, cmdline, nullptr)) {
            LOG_ERROR(FRONTEND_CMDLINE, "Failed to launch app with UID: {}", app_name_str);
            return -2;
        }
        LOG_INFO(FRONTEND_CMDLINE, "App launched by UID: {}", app_name_str);
    } else {
        // Search by name
        std::vector<apa_app_registry> &regs = svr->get_registerations();
        LOG_INFO(FRONTEND_CMDLINE, "Searching {} app registrations for '{}'", regs.size(), app_name_str);

        apa_app_registry *found = nullptr;
        for (auto &reg : regs) {
            std::string caption = common::ucs2_to_utf8(reg.mandatory_info.long_caption.to_std_string(nullptr));
            if (caption == app_name_str) {
                found = &reg;
                break;
            }
        }

        if (!found) {
            LOG_ERROR(FRONTEND_CMDLINE, "No app found with name: '{}'", app_name_str);
            LOG_INFO(FRONTEND_CMDLINE, "Available apps:");
            for (auto &reg : regs) {
                std::string caption = common::ucs2_to_utf8(reg.mandatory_info.long_caption.to_std_string(nullptr));
                if (!caption.empty()) {
                    LOG_INFO(FRONTEND_CMDLINE, "  - '{}' (UID: 0x{:08x})", caption, reg.mandatory_info.uid);
                }
            }
            return -2;
        }

        epoc::apa::command_line cmdline;
        cmdline.launch_cmd_ = epoc::apa::command_create;
        if (!svr->launch_app(*found, cmdline, nullptr)) {
            LOG_ERROR(FRONTEND_CMDLINE, "Failed to launch app: {}", app_name_str);
            return -2;
        }
        LOG_INFO(FRONTEND_CMDLINE, "App launched: {}", app_name_str);
    }

    g_state->winserv = reinterpret_cast<window_server *>(
        g_state->symsys->get_kernel_system()->get_by_name<service::server>(
            get_winserv_name_by_epocver(g_state->symsys->get_symbian_version_use())));

    // Set up default key bindings (driver key codes -> Symbian scan codes)
    if (g_state->winserv) {
        auto &kmap = g_state->winserv->input_mapping.key_input_map;
        kmap[257] = epoc::std_key_enter;           // Enter -> EStdKeyEnter
        kmap[256] = epoc::std_key_escape;          // Esc -> EStdKeyEscape
        kmap[265] = epoc::std_key_up_arrow;        // Up -> EStdKeyUpArrow
        kmap[264] = epoc::std_key_down_arrow;      // Down -> EStdKeyDownArrow
        kmap[263] = epoc::std_key_left_arrow;      // Left -> EStdKeyLeftArrow
        kmap[262] = epoc::std_key_right_arrow;     // Right -> EStdKeyRightArrow
        kmap[290] = epoc::std_key_application_0;   // F1 -> Left softkey
        kmap[291] = epoc::std_key_application_1;   // F2 -> Right softkey
        kmap[325] = epoc::std_key_device_3;        // Numpad5 -> OK/Select
        kmap[320] = epoc::std_key_nkp_0;           // Numpad0
        kmap[321] = epoc::std_key_nkp_1;           // Numpad1
        kmap[322] = epoc::std_key_nkp_2;           // Numpad2
        kmap[323] = epoc::std_key_nkp_3;           // Numpad3
        kmap[324] = epoc::std_key_nkp_4;           // Numpad4
        kmap[326] = epoc::std_key_nkp_6;           // Numpad6
        kmap[327] = epoc::std_key_nkp_7;           // Numpad7
        kmap[328] = epoc::std_key_nkp_8;           // Numpad8
        kmap[329] = epoc::std_key_nkp_9;           // Numpad9
        LOG_INFO(FRONTEND_CMDLINE, "Default key bindings configured ({} mappings)", kmap.size());
    }

    // Register screen redraw callback to present frames (like Qt frontend does)
    if (g_state->winserv) {
        epoc::screen *scr = g_state->winserv->get_screens();
        if (scr) {
            g_state->screen_redraw_cb_id = scr->add_screen_redraw_callback(
                nullptr, [](void *, epoc::screen *scr, const bool) {
                    if (!g_state || !g_state->graphics_driver) return;

                    g_state->graphics_driver->wait_for(&g_state->present_status);

                    drivers::graphics_command_builder builder;
                    auto &crr_mode = scr->current_mode();

                    eka2l1::vec2 screen_size(crr_mode.size);

                    // Match Qt: use screen size as swapchain size
                    eka2l1::vec2 swapchain_size = screen_size;

                    // Only resize canvas when size actually changes — resizing
                    // clears the WebGL framebuffer on every call.
                    static eka2l1::vec2 last_surface_size = { 0, 0 };
                    if (swapchain_size != last_surface_size) {
                        g_state->graphics_driver->update_surface_size(swapchain_size);
                        last_surface_size = swapchain_size;
                    }

                    builder.set_swapchain_size(swapchain_size);
                    builder.backup_state();

                    // Match Qt: same feature setup order
                    builder.set_feature(drivers::graphics_feature::cull, false);
                    builder.set_feature(drivers::graphics_feature::depth_test, false);
                    builder.set_feature(drivers::graphics_feature::blend, false);
                    builder.set_feature(drivers::graphics_feature::stencil_test, false);
                    builder.set_feature(drivers::graphics_feature::clipping, false);

                    eka2l1::rect viewport;
                    viewport.size = swapchain_size;
                    builder.set_viewport(viewport);

                    builder.clear({ 0.816f, 0.816f, 0.816f, 1.0f, 0.0f, 0.0f }, drivers::draw_buffer_bit_color_buffer);

                    // Match Qt: set texture filter before drawing
                    builder.set_texture_filter(scr->screen_texture, true, drivers::filter_option::linear);
                    builder.set_texture_filter(scr->screen_texture, false, drivers::filter_option::linear);

                    eka2l1::rect dest;
                    dest.size = swapchain_size;

                    eka2l1::rect src;
                    src.size = screen_size;
                    src.size *= scr->display_scale_factor;

                    builder.draw_bitmap(scr->screen_texture, 0, dest, src, eka2l1::vec2(0, 0), 0.0f, 0);

                    builder.load_backup_state();

                    g_state->present_status = -100;
                    builder.present(&g_state->present_status);

                    auto cmd_list = builder.retrieve_command_list();
                    g_state->graphics_driver->submit_command_list(cmd_list);
                });
            LOG_INFO(FRONTEND_CMDLINE, "Screen redraw callback registered");
        } else {
            LOG_WARN(FRONTEND_CMDLINE, "No screen available for redraw callback");
        }
    } else {
        LOG_WARN(FRONTEND_CMDLINE, "Window server not found");
    }

    g_state->running = true;

    // Run the emulator loop on a background thread so the main thread stays free
    g_state->emu_thread = std::make_unique<std::thread>([]() {
        LOG_INFO(FRONTEND_CMDLINE, "Emulator thread started");
        int iterations = 0;
        auto last_forced_redraw = std::chrono::steady_clock::now();
        while (g_state && g_state->running) {
            int ret = g_state->symsys->loop();
            iterations++;
            if (ret == 0) {
                g_state->running = false;
                break;
            }

            // Periodically force a screen redraw to ensure the display stays
            // updated. The animation scheduler and posting surface handle most
            // redraws, but a forced redraw ensures FLAG_SERVER_REDRAW_PENDING
            // is set so that the screen composites the full window tree
            // (not just DSA/posting content) on each frame.
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_forced_redraw).count() >= 100) {
                last_forced_redraw = now;
                if (g_state->winserv) {
                    epoc::screen *scr = g_state->winserv->get_screens();
                    if (scr && g_state->graphics_driver) {
                        g_state->symsys->get_kernel_system()->lock();
                        {
                            const std::lock_guard<std::mutex> guard(scr->screen_mutex);
                            scr->need_update_visible_regions(true);
                            scr->set_server_redraw_pending();
                            scr->redraw(g_state->graphics_driver.get());
                        }
                        g_state->symsys->get_kernel_system()->unlock();
                    }
                }
            }
        }
        LOG_INFO(FRONTEND_CMDLINE, "Emulator loop exited after {} iterations", iterations);
    });

    return 0;
}

EMSCRIPTEN_KEEPALIVE
void eka2l1_press_key(int key_code) {
    if (!g_state || !g_state->winserv) return;

    drivers::input_event press_evt;
    press_evt.type_ = drivers::input_event_type::key;
    press_evt.key_.state_ = drivers::key_state::pressed;
    press_evt.key_.code_ = key_code;
    g_state->winserv->queue_input_from_driver(press_evt);

    drivers::input_event release_evt;
    release_evt.type_ = drivers::input_event_type::key;
    release_evt.key_.state_ = drivers::key_state::released;
    release_evt.key_.code_ = key_code;
    g_state->winserv->queue_input_from_driver(release_evt);
}

EMSCRIPTEN_KEEPALIVE
int eka2l1_get_distinct_colors() {
    if (!g_state) return 0;
    const std::lock_guard<std::mutex> guard(g_state->pixel_mutex);
    return g_state->pixel_distinct_colors;
}

EMSCRIPTEN_KEEPALIVE
void eka2l1_start_frame_dump(const char *output_dir, int total_frames) {
    if (!g_state) return;
    g_state->dumper = std::make_unique<common::frame_dumper>(
        output_dir ? output_dir : "/tmp/frames", total_frames > 0 ? total_frames : 16);
    LOG_INFO(FRONTEND_CMDLINE, "Frame dump started: dir={}, frames={}", output_dir, total_frames);
}

EMSCRIPTEN_KEEPALIVE
int eka2l1_frame_dump_done() {
    if (!g_state || !g_state->dumper) return 1;
    return g_state->dumper->done() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int eka2l1_frame_dump_captured() {
    if (!g_state || !g_state->dumper) return 0;
    return g_state->dumper->captured();
}

EMSCRIPTEN_KEEPALIVE
void eka2l1_shutdown() {
    if (g_state) {
        g_state->running = false;
        if (g_state->graphics_driver) {
            g_state->graphics_driver->abort();
        }
        if (g_state->gfx_thread && g_state->gfx_thread->joinable()) {
            g_state->gfx_thread->join();
        }
        if (g_state->emu_thread && g_state->emu_thread->joinable()) {
            g_state->emu_thread->join();
        }
        g_state->symsys.reset();
        g_state->graphics_driver.reset();
        g_state->audio_driver.reset();
        delete g_state;
        g_state = nullptr;
    }
}

} // extern "C"

int main() {
    emscripten_exit_with_live_runtime();
    return 0;
}
