/*
 * Copyright (c) 2026 EKA2L1 Team.
 * This file is part of EKA2L1 project and is licensed under GPLv3 or later.
 */

#include <common/cvt.h>
#include <common/deterministic.h>
#include <common/log.h>
#include <common/path.h>
#include <config/app_settings.h>
#include <config/config.h>
#include <cpu/arm_interface.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <drivers/audio/clocked.h>
#include <drivers/graphics/graphics.h>
#include <drivers/graphics/backend/graphics_driver_shared.h>
#include <drivers/input/common.h>
#include <drivers/itc.h>
#include <kernel/kernel.h>
#include <kernel/codeseg.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <package/manager.h>
#include <services/applist/applist.h>
#include <services/window/screen.h>
#include <services/window/window.h>
#include <system/deterministic.h>
#include <system/devices.h>
#include <system/epoc.h>
#include <utils/apacmd.h>
#include <vfs/vfs.h>
#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <malloc.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

using namespace eka2l1;

namespace {
    struct cooperative_state {
        config::state conf;
        std::unique_ptr<config::app_settings> settings;
        drivers::audio_driver_instance audio;
        std::unique_ptr<drivers::graphics_driver> graphics;
        std::unique_ptr<eka2l1::system> system;
        window_server *windows = nullptr;
        std::vector<std::uint8_t> rgba;
        std::string error;
        std::string report;
        std::uint32_t presentations = 0;
        std::uint32_t launched_uid = 0;
        std::uint64_t present_virtual_us = 0, present_instructions = 0;
        std::string code_witness = "null";
        int width = 0, height = 0;
        bool running = false, busy = false, capture = false;
    };
    std::unique_ptr<cooperative_state> state;

    template<typename F> int checked(F operation) {
        if (!state || state->busy) return -1;
        state->busy = true;
        try {
            const int result = operation();
            state->busy = false;
            return result;
        } catch (const std::exception &error) {
            state->error = error.what();
        } catch (...) {
            state->error = "Unknown cooperative runtime failure";
        }
        state->running = false;
        state->busy = false;
        return -1;
    }

    void present() {
        if (!state || !state->windows) return;
        auto *screen = state->windows->get_screens();
        if (!screen || !screen->screen_texture) return;
        const auto size = screen->current_mode().size;
        if (size.x <= 0 || size.y <= 0 || size.x > 2048 || size.y > 2048)
            throw std::runtime_error("Invalid cooperative framebuffer extent");
        state->width = size.x;
        state->height = size.y;
        if (state->capture) {
            state->rgba.resize(size.x * size.y * 4);
            auto *driver = static_cast<drivers::shared_graphics_driver *>(state->graphics.get());
            if (!driver->read_bitmap_pixels(screen->screen_texture, size.x, size.y, state->rgba.data()))
                throw std::runtime_error("Cooperative framebuffer readback failed");
        }
        ++state->presentations;
        state->present_virtual_us = common::benchmark::virtual_us.load();
        state->present_instructions = common::benchmark::instructions.load();
    }
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_js_replay(const char *file) {
    if (state || !file || !*file) return -1;
    return setenv("EKA2L1_BENCHMARK_INPUT", file, 1);
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_init(const char *storage) {
    if (state || !storage || !*storage) return -1;
    setenv("EKA2L1_BENCHMARK", "1", 1);
    common::benchmark::interactive = !std::getenv("EKA2L1_BENCHMARK_INPUT");
    common::benchmark::paced = false;
    common::benchmark::retain_audio = false;
    log::setup_log(nullptr);
    state = std::make_unique<cooperative_state>();
    return checked([&]() {
        state->conf.deserialize();
        state->conf.storage = storage;
        state->conf.cpu_backend = "dyncom";
        state->settings = std::make_unique<config::app_settings>(&state->conf);
        state->audio = drivers::make_clocked_audio_driver(false, true);
        if (!state->audio) throw std::runtime_error("Clocked audio unavailable");
        state->audio->set_bank_path(drivers::MIDI_BANK_TYPE_HSB, state->conf.hsb_bank_path);
        state->audio->set_bank_path(drivers::MIDI_BANK_TYPE_SF2, state->conf.sf2_bank_path);
        system_create_components components;
        components.audio_ = state->audio.get();
        components.conf_ = &state->conf;
        components.settings_ = state->settings.get();
        state->system = std::make_unique<eka2l1::system>(components);
        if (!state->system->get_device_manager()->total())
            throw std::runtime_error("Prepared device image has no devices");
        state->system->startup();
        if (!state->system->set_device(state->conf.device))
            throw std::runtime_error("Prepared device selection failed");
        for (const auto drive : {drive_c, drive_d, drive_e, drive_z}) {
            const char letter = char('a' + int(drive));
            const auto root = add_path(state->conf.storage, std::string("/drives/") + letter + "/");
            state->system->mount(drive, drive == drive_z ? drive_media::rom : drive_media::physical,
                root, drive == drive_z ? io_attrib_internal | io_attrib_write_protected :
                drive == drive_e ? io_attrib_removeable : io_attrib_internal);
        }
        state->system->initialize_user_parties();
        state->system->get_packages()->load_registries();
        state->system->get_packages()->migrate_legacy_registries();
        drivers::window_system_info info;
        info.type = drivers::window_system_type::emscripten;
        state->graphics = drivers::create_graphics_driver(drivers::graphic_api::opengl, info);
        if (!state->graphics) throw std::runtime_error("WebGL graphics unavailable");
        state->system->set_graphics_driver(state->graphics.get());
        state->graphics->set_display_hook(present);
        return 0;
    });
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_install_sis(const char *file) {
    return checked([&]() {
        if (state->running || !file || !*file) return -1;
        return state->system->get_packages()->install_package(common::utf8_to_ucs2(file), drive_e,
            nullptr, nullptr) == package::installation_result_success ? 0 : -1;
    });
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_capture(int enabled) {
    return checked([&]() {
        if (state->running || (enabled != 0 && enabled != 1)) return -1;
        state->capture = enabled != 0;
        state->rgba.clear();
        return 0;
    });
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_launch(std::uint32_t uid) {
    return checked([&]() {
        if (state->running) return -1;
        auto *server = reinterpret_cast<applist_server *>(state->system->get_kernel_system()->
            get_by_name<service::server>(get_app_list_server_name_by_epocver(state->system->get_symbian_version_use())));
        if (!server) throw std::runtime_error("App list service unavailable");
        auto *registration = server->get_registration(uid);
        if (!registration) throw std::runtime_error("Application UID is not installed");
        epoc::apa::command_line command;
        command.launch_cmd_ = epoc::apa::command_create;
        if (!server->launch_app(*registration, command, nullptr))
            throw std::runtime_error("Application launch failed");
        state->windows = reinterpret_cast<window_server *>(state->system->get_kernel_system()->
            get_by_name<service::server>(get_winserv_name_by_epocver(state->system->get_symbian_version_use())));
        if (!state->windows || !state->windows->get_screens())
            throw std::runtime_error("Window service unavailable");
        state->windows->get_screens()->add_screen_redraw_callback(nullptr,
            [](void *, epoc::screen *screen, const bool) {
                drivers::graphics_command_builder builder;
                const auto size = screen->current_mode().size;
                state->graphics->update_surface_size(size);
                builder.set_swapchain_size(size);
                builder.backup_state();
                for (const auto feature : {drivers::graphics_feature::cull, drivers::graphics_feature::depth_test,
                    drivers::graphics_feature::blend, drivers::graphics_feature::stencil_test,
                    drivers::graphics_feature::clipping}) builder.set_feature(feature, false);
                eka2l1::rect viewport; viewport.size = size;
                builder.set_viewport(viewport);
                builder.clear({0.816f, 0.816f, 0.816f, 1.0f, 0.0f, 0.0f}, drivers::draw_buffer_bit_color_buffer);
                builder.set_texture_filter(screen->screen_texture, true, drivers::filter_option::linear);
                builder.set_texture_filter(screen->screen_texture, false, drivers::filter_option::linear);
                eka2l1::rect destination; destination.size = size;
                eka2l1::rect source = destination; source.size *= screen->display_scale_factor;
                builder.draw_bitmap(screen->screen_texture, 0, destination, source, {0, 0}, 0.0f, 0);
                builder.load_backup_state();
                int status = -100;
                builder.present(&status);
                auto list = builder.retrieve_command_list();
                state->graphics->submit_command_list(list);
                if (status != 0) throw std::runtime_error("Presentation did not complete synchronously");
            });
        start_benchmark_input(state->system.get(), state->windows);
        state->launched_uid = uid;
        state->running = true;
        return 0;
    });
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_tick(unsigned iterations, unsigned wall_ms) {
    return checked([&]() {
        if (!state->running || !iterations || iterations > 100000 || !wall_ms || wall_ms > 50) return -1;
        const double deadline = emscripten_get_now() + wall_ms;
        const auto guest_deadline = common::benchmark::virtual_us.load() + 10000;
        const auto presented = state->presentations;
        unsigned completed = 0;
        do {
            std::uint32_t pc = 0, begin = 0, size = 0;
            const bool inspect = state->capture && state->code_witness == "null";
            const auto before = inspect ? common::benchmark::instructions.load() : 0;
            if (inspect) {
                auto *thread = state->system->get_kernel_system()->crr_thread();
                auto *process = thread ? thread->owning_process() : nullptr;
                auto *code = process ? process->get_codeseg() : nullptr;
                if (code && process->get_uid() == state->launched_uid) {
                    begin = code->get_code_run_addr(process);
                    size = code->get_code_size();
                    pc = state->system->get_cpu()->get_pc();
                }
            }
            if (!state->system->loop()) { state->running = false; break; }
            const auto after = size ? common::benchmark::instructions.load() : 0;
            if (size && pc >= begin && std::uint64_t(pc) < std::uint64_t(begin) + size && after > before) {
                std::ostringstream witness;
                witness << "{\"uid\":" << state->launched_uid << ",\"pc_before_loop\":" << pc
                    << ",\"code_begin\":" << begin << ",\"code_bytes\":" << size
                    << ",\"instructions_before\":" << before << ",\"instructions_after\":" << after << "}";
                state->code_witness = witness.str();
            }
            ++completed;
        } while (completed < iterations && emscripten_get_now() < deadline &&
            common::benchmark::virtual_us.load() < guest_deadline && state->presentations == presented);
        return int(completed);
    });
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_key(int code, int down) {
    return checked([&]() {
        if (!state->running || (down != 0 && down != 1) || code < 0 || code > 65535) return -1;
        drivers::input_event event{};
        event.type_ = drivers::input_event_type::key_raw;
        event.key_.code_ = code;
        event.key_.state_ = down ? drivers::key_state::pressed : drivers::key_state::released;
        state->windows->queue_input_from_driver(event);
        return 0;
    });
}

EMSCRIPTEN_KEEPALIVE const void *eka2l1_js_frame() {
    return state && !state->busy && state->capture && !state->rgba.empty() ? state->rgba.data() : nullptr;
}
EMSCRIPTEN_KEEPALIVE int eka2l1_js_audio(std::int16_t *out, unsigned frames) {
    if (!state || state->busy || !out || !frames || frames > 4096) return -1;
    return int(drivers::read_clocked_audio(out, frames));
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_status() {
    if (!state || state->busy) return "{}";
    std::ostringstream out;
    const auto allocation = mallinfo();
    out << "{\"running\":" << (state->running ? "true" : "false")
        << ",\"virtual_us\":" << common::benchmark::virtual_us.load()
        << ",\"instructions\":" << common::benchmark::instructions.load()
        << ",\"presentations\":" << state->presentations
        << ",\"present_virtual_us\":" << state->present_virtual_us
        << ",\"present_instructions\":" << state->present_instructions
        << ",\"width\":" << state->width << ",\"height\":" << state->height
        << ",\"capture\":" << (state->capture ? "true" : "false")
        << ",\"code_witness\":" << state->code_witness
        << ",\"heap\":{\"allocated\":" << allocation.uordblks
        << ",\"free\":" << allocation.fordblks << ",\"arena\":" << allocation.arena
        << ",\"end\":" << emscripten_get_heap_size()
        << ",\"break\":" << reinterpret_cast<std::uintptr_t>(sbrk(0)) << "}"
        << ",\"audio\":" << drivers::clocked_audio_stats() << "}";
    if (state->capture && state->system->get_cpu()) {
        const auto cache = static_cast<arm::dyncom_core *>(state->system->get_cpu())->interpreter_cache_stats();
        auto report = out.str();
        report.pop_back();
        std::ostringstream decoder;
        decoder << ",\"decoder\":{\"reserved\":" << cache.reserved_bytes << ",\"used\":" << cache.used_bytes
            << ",\"blocks\":" << cache.blocks << ",\"buckets\":" << cache.buckets << "}}";
        state->report = report + decoder.str();
    } else {
        state->report = out.str();
    }
    return state->report.c_str();
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_error() { return state ? state->error.c_str() : "Not initialized"; }
EMSCRIPTEN_KEEPALIVE int eka2l1_js_shutdown() {
    if (!state || state->busy) return -1;
    state->system.reset();
    state->graphics.reset();
    state->audio.reset();
    state.reset();
    return 0;
}
}
