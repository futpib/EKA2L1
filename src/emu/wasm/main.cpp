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
#include <common/log.h>
#include <common/path.h>
#include <common/types.h>
#include <common/version.h>

#include <config/app_settings.h>
#include <config/config.h>

#include <drivers/audio/audio.h>
#include <drivers/graphics/graphics.h>

#include <kernel/kernel.h>
#include <package/manager.h>
#include <services/init.h>
#include <services/window/window.h>
#include <system/devices.h>
#include <system/epoc.h>
#include <system/installation/install_device.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <memory>

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

    void main_loop() {
        if (!g_state || !g_state->running) {
            return;
        }

        g_state->symsys->loop();
    }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int eka2l1_init(const char *data_path) {
    log::setup_log(nullptr);
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

    auto progress_cb = [](const std::uint64_t so_far, const std::uint64_t total) {
        if (total > 0) {
            int pct = static_cast<int>(so_far * 100 / total);
            LOG_INFO(FRONTEND_CMDLINE, "Installing device: {}%", pct);
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

    if (!pkgmngr->install_package(common::utf8_to_ucs2(sis_path), drive_e, nullptr, nullptr)) {
        LOG_ERROR(FRONTEND_CMDLINE, "SIS installation failed: {}", sis_path);
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

    // Create graphics driver (WebGL context)
    drivers::window_system_info wsi;
    wsi.type = drivers::window_system_type::emscripten;

    g_state->graphics_driver = drivers::create_graphics_driver(
        drivers::graphic_api::opengl, wsi);

    if (!g_state->graphics_driver) {
        LOG_ERROR(FRONTEND_CMDLINE, "Failed to create graphics driver");
        return -1;
    }

    g_state->symsys->set_graphics_driver(g_state->graphics_driver.get());

    // Create audio driver
    g_state->audio_driver = drivers::make_audio_driver(
        drivers::audio_driver_backend::cubeb, g_state->conf.audio_master_volume);

    if (g_state->audio_driver) {
        g_state->symsys->set_audio_driver(g_state->audio_driver.get());
    }

    // Launch the app
    LOG_INFO(FRONTEND_CMDLINE, "Launching: {}", app_name);
    g_state->symsys->load(common::utf8_to_ucs2(app_name), u"");

    g_state->winserv = reinterpret_cast<window_server *>(
        g_state->symsys->get_kernel_system()->get_by_name<service::server>(
            get_winserv_name_by_epocver(g_state->symsys->get_symbian_version_use())));

    g_state->running = true;

    // Use emscripten main loop — yields back to browser each frame
    emscripten_set_main_loop(main_loop, 0, 0);

    return 0;
}

EMSCRIPTEN_KEEPALIVE
void eka2l1_shutdown() {
    if (g_state) {
        g_state->running = false;
        g_state->symsys.reset();
        g_state->graphics_driver.reset();
        g_state->audio_driver.reset();
        delete g_state;
        g_state = nullptr;
    }
}

} // extern "C"

int main() {
    return 0;
}
