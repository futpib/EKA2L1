/*
 * Copyright (c) 2026 EKA2L1 Team.
 * This file is part of EKA2L1 project and is licensed under GPLv3 or later.
 */

#include "slice-census.h"
#include "code-capture.h"

#include <common/cvt.h>
#include <common/deterministic.h>
#include <common/log.h>
#include <common/path.h>
#include <config/app_settings.h>
#include <config/config.h>
#include <cpu/arm_interface.h>
#if defined(EKA2L1_COUNTED_JS)
#include <cpu/aot/counted_js.h>
extern "C" int eka2l1_counted_configure(int diagnostics);
#endif
#if defined(EKA2L1_COUNTED_LOOP_JS)
#include <cpu/aot/loop_js.h>
extern "C" int eka2l1_loop_configure(int diagnostics);
#if defined(EKA2L1_PACKED_PIXEL_JS)
extern "C" int eka2l1_loop_packed_pixel_configure(int enabled);
#endif
#endif
#if defined(EKA2L1_COUNTED_REGION_JS)
#include <cpu/aot/region_js.h>
extern "C" int eka2l1_region_configure(int diagnostics);
#endif
#if defined(EKA2L1_WORD_INCREMENT_JS)
#include <cpu/aot/word_increment.h>
#endif
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
#include <iomanip>
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
        std::unique_ptr<cooperative_diagnostics::slice_census> census;
        std::vector<cooperative_diagnostics::mapped_code_capture> mapped_code;
        int width = 0, height = 0;
        bool running = false, busy = false, capture = false;
#if defined(EKA2L1_COUNTED_LOOP_JS)
        bool loop_diagnostics = false;
#endif
#if defined(EKA2L1_COUNTED_REGION_JS)
        bool region_diagnostics = false;
#endif
#if defined(EKA2L1_WORD_INCREMENT_JS)
        bool word_increment_diagnostics = false;
#endif
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

    std::string json_quote(const std::string &text) {
        std::ostringstream out; out << '"';
        for (const unsigned char c : text) {
            if (c == '"' || c == '\\') out << '\\' << c;
            else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
            else out << c;
        }
        out << '"'; return out.str();
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

EMSCRIPTEN_KEEPALIVE int eka2l1_js_diagnostics(int enabled) {
    return checked([&]() {
        if (state->running || (enabled != 0 && enabled != 1)) return -1;
        state->census = enabled ? std::make_unique<cooperative_diagnostics::slice_census>() : nullptr;
        return 0;
    });
}

EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_census() {
    if (!state || state->busy || !state->census) return "{}";
    state->report = state->census->json();
    return state->report.c_str();
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_counted(int diagnostics) {
    return checked([&]() {
        if (state->running || (diagnostics != 0 && diagnostics != 1)) return -1;
#if defined(EKA2L1_COUNTED_JS)
#if defined(EKA2L1_COUNTED_REGION_JS)
        arm::region_js::reset();
        state->region_diagnostics = false;
#endif
#if defined(EKA2L1_COUNTED_LOOP_JS)
        arm::loop_js::reset();
        state->loop_diagnostics = false;
#endif
        return eka2l1_counted_configure(diagnostics);
#else
        return -1;
#endif
    });
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_counted_status() {
    if (!state || state->busy) return "{}";
#if defined(EKA2L1_COUNTED_JS)
    const bool detailed = arm::counted_js::diagnostics();
    std::ostringstream out;
    out << "{\"available\":true,\"enabled\":" << (arm::counted_js::enabled ? "true" : "false")
        << ",\"diagnostics\":" << (detailed ? "true" : "false") << ",\"calls\":";
    if (detailed) out << arm::counted_js::calls(); else out << "null";
    out << ",\"instructions\":";
    if (detailed) out << arm::counted_js::instructions(); else out << "null";
    out << '}'; state->report = out.str(); return state->report.c_str();
#else
    return "{\"available\":false,\"enabled\":false,\"diagnostics\":false,\"calls\":null,\"instructions\":null}";
#endif
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_loop(int diagnostics) {
    return checked([&]() {
        if (state->running || (diagnostics != 0 && diagnostics != 1)) return -1;
#if defined(EKA2L1_COUNTED_LOOP_JS)
#if defined(EKA2L1_COUNTED_REGION_JS)
        arm::region_js::reset();
        state->region_diagnostics = false;
#endif
        state->loop_diagnostics = false;
        const int entries = eka2l1_loop_configure(diagnostics);
        if (entries > 0) state->loop_diagnostics = diagnostics != 0;
        return entries;
#else
        return -1;
#endif
    });
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_loop_status() {
    if (!state || state->busy) return "{}";
#if defined(EKA2L1_COUNTED_LOOP_JS)
    const bool detailed = state->loop_diagnostics;
    std::ostringstream out;
    out << "{\"available\":true,\"enabled\":" << (arm::loop_js::enabled ? "true" : "false")
        << ",\"diagnostics\":" << (detailed ? "true" : "false") << ",\"calls\":";
    if (detailed) out << arm::loop_js::calls(); else out << "null";
    out << ",\"instructions\":";
    if (detailed) out << arm::loop_js::instructions(); else out << "null";
    out << '}'; state->report = out.str(); return state->report.c_str();
#else
    return "{\"available\":false,\"enabled\":false,\"diagnostics\":false,\"calls\":null,\"instructions\":null}";
#endif
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_packed_pixel(int enabled) {
    return checked([&]() {
        if (state->running || (enabled != 0 && enabled != 1)) return -1;
#if defined(EKA2L1_PACKED_PIXEL_JS)
        return eka2l1_loop_packed_pixel_configure(enabled);
#else
        return -1;
#endif
    });
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_packed_pixel_status() {
    if (!state || state->busy) return "{}";
#if defined(EKA2L1_PACKED_PIXEL_JS)
    const bool detailed = state->loop_diagnostics;
    std::ostringstream out;
    out << "{\"available\":true,\"enabled\":" << (arm::loop_js::packed_pixel_enabled ? "true" : "false")
        << ",\"diagnostics\":" << (detailed ? "true" : "false") << ",\"calls\":";
    if (detailed) out << arm::loop_js::packed_pixel_calls(); else out << "null";
    out << ",\"iterations\":";
    if (detailed) out << arm::loop_js::packed_pixel_iterations(); else out << "null";
    out << ",\"instructions\":";
    if (detailed) out << arm::loop_js::packed_pixel_iterations() * 18; else out << "null";
    out << ",\"declines\":";
    if (detailed) out << arm::loop_js::packed_pixel_declines(); else out << "null";
    out << '}'; state->report = out.str(); return state->report.c_str();
#else
    return "{\"available\":false,\"enabled\":false,\"diagnostics\":false,\"calls\":null,\"iterations\":null,\"instructions\":null,\"declines\":null}";
#endif
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_region(int diagnostics) {
    return checked([&]() {
        if (state->running || (diagnostics != 0 && diagnostics != 1)) return -1;
#if defined(EKA2L1_COUNTED_REGION_JS)
        state->region_diagnostics = false;
        const int entries = eka2l1_region_configure(diagnostics);
        if (entries > 0) state->region_diagnostics = diagnostics != 0;
        return entries;
#else
        return -1;
#endif
    });
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_region_status() {
    if (!state || state->busy) return "{}";
#if defined(EKA2L1_COUNTED_REGION_JS)
    const bool detailed = state->region_diagnostics;
    std::ostringstream out;
    out << "{\"available\":true,\"enabled\":" << (arm::region_js::enabled ? "true" : "false")
        << ",\"diagnostics\":" << (detailed ? "true" : "false") << ",\"calls\":";
    if (detailed) out << arm::region_js::calls(); else out << "null";
    out << ",\"instructions\":";
    if (detailed) out << arm::region_js::instructions(); else out << "null";
    out << '}'; state->report = out.str(); return state->report.c_str();
#else
    return "{\"available\":false,\"enabled\":false,\"diagnostics\":false,\"calls\":null,\"instructions\":null}";
#endif
}

EMSCRIPTEN_KEEPALIVE int eka2l1_js_word_increment(int diagnostics) {
    return checked([&]() {
        if (state->running || (diagnostics != 0 && diagnostics != 1)) return -1;
#if defined(EKA2L1_WORD_INCREMENT_JS)
        arm::word_increment::install(diagnostics != 0);
        state->word_increment_diagnostics = diagnostics != 0;
        return 1;
#else
        return -1;
#endif
    });
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_word_increment_status() {
    if (!state || state->busy) return "{}";
#if defined(EKA2L1_WORD_INCREMENT_JS)
    const bool detailed = state->word_increment_diagnostics;
    std::ostringstream out;
    out << "{\"available\":true,\"enabled\":" << (arm::word_increment::enabled ? "true" : "false")
        << ",\"diagnostics\":" << (detailed ? "true" : "false") << ",\"calls\":";
    if (detailed) out << arm::word_increment::calls(); else out << "null";
    out << ",\"iterations\":";
    if (detailed) out << arm::word_increment::iterations(); else out << "null";
    out << ",\"instructions\":";
    if (detailed) out << arm::word_increment::iterations() * 5; else out << "null";
    out << ",\"declines\":";
    if (detailed) out << arm::word_increment::declines(); else out << "null";
    out << ",\"declines_scope\":\"Authenticated candidate preflight refusals only\"}";
    state->report = out.str(); return state->report.c_str();
#else
    return "{\"available\":false,\"enabled\":false,\"diagnostics\":false,\"calls\":null,\"iterations\":null,\"instructions\":null,\"declines\":null}";
#endif
}

// Explicit once-only paused-boundary code export for offline translation.
EMSCRIPTEN_KEEPALIVE int eka2l1_js_code_capture(std::uint32_t process_uid, const char *names) {
    return checked([&]() {
        if (!state->running || !process_uid || !names || !*names || !state->mapped_code.empty()) return -1;
        const std::string requested(names);
        if (requested.size() > 4096) return -1;
        std::vector<std::string> modules;
        std::size_t begin = 0;
        do {
            const auto end = requested.find(',', begin);
            const auto name = requested.substr(begin, end == std::string::npos ? end : end - begin);
            if (name.empty()) return -1;
            modules.push_back(name);
            if (end == std::string::npos) break;
            begin = end + 1;
        } while (true);
        state->mapped_code = cooperative_diagnostics::capture_mapped_code(*state->system,
            process_uid, modules, common::benchmark::virtual_us.load(), common::benchmark::instructions.load());
        return int(state->mapped_code.size());
    });
}
EMSCRIPTEN_KEEPALIVE int eka2l1_js_code_capture_selected(const char *selection) {
    return checked([&]() {
        if (!state->running || !selection || !*selection || !state->mapped_code.empty()) return -1;
        const std::string requested(selection);
        if (requested.size() > 8192 || requested.back() == ';') return -1;
        const auto virtual_us = common::benchmark::virtual_us.load();
        const auto instructions = common::benchmark::instructions.load();
        const auto generation = mem::mapping_generation.load(std::memory_order_acquire);
        std::vector<cooperative_diagnostics::mapped_code_capture> captured;
        std::set<std::pair<std::uint32_t, std::string>> identities;
        std::uint64_t total = 0;
        std::size_t begin = 0;
        do {
            const auto end = requested.find(';', begin);
            const auto group = requested.substr(begin, end == std::string::npos ? end : end - begin);
            const auto colon = group.find(':');
            if (colon == std::string::npos || !colon || colon + 1 == group.size()) return -1;
            const auto uid_text = group.substr(0, colon);
            if (uid_text.find_first_not_of("0123456789") != std::string::npos) return -1;
            const auto parsed_uid = std::stoul(uid_text);
            if (!parsed_uid || parsed_uid > 0xffffffffu) return -1;
            const auto process_uid = static_cast<std::uint32_t>(parsed_uid);
            const auto names = group.substr(colon + 1);
            std::vector<std::string> modules;
            std::size_t name_begin = 0;
            do {
                const auto name_end = names.find(',', name_begin);
                const auto name = names.substr(name_begin, name_end == std::string::npos ? name_end : name_end - name_begin);
                if (name.empty() || name.size() > 128
                        || std::any_of(name.begin(), name.end(), [](unsigned char c) {
                            return c < 32 || c > 126 || c == ':' || c == '/' || c == '\\';
                        })
                        || !identities.emplace(process_uid, cooperative_diagnostics::code_capture_name(name)).second)
                    return -1;
                modules.push_back(name);
                if (identities.size() > 32) return -1;
                if (name_end == std::string::npos) break;
                name_begin = name_end + 1;
            } while (true);
            auto rows = cooperative_diagnostics::capture_mapped_code(*state->system,
                process_uid, modules, virtual_us, instructions);
            for (auto &row : rows) {
                if (row.generation != generation || (total += row.bytes) > 64u * 1024u * 1024u)
                    throw std::runtime_error("Grouped mapped-code capture identity or extent changed");
                captured.push_back(std::move(row));
            }
            if (end == std::string::npos) break;
            begin = end + 1;
        } while (true);
        if (common::benchmark::virtual_us.load() != virtual_us
                || common::benchmark::instructions.load() != instructions
                || mem::mapping_generation.load(std::memory_order_acquire) != generation)
            throw std::runtime_error("Grouped mapped-code capture boundary changed");
        state->mapped_code = std::move(captured);
        return int(state->mapped_code.size());
    });
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_js_code_metadata() {
    if (!state || state->busy) return "[]";
    std::ostringstream out; out << '[';
    bool comma = false;
    for (const auto &row : state->mapped_code) {
        if (comma) out << ',';
        comma = true;
        out << "{\"name\":" << json_quote(row.name) << ",\"process_name\":" << json_quote(row.process_name)
            << ",\"uid\":" << row.uid << ",\"process_uid\":" << row.process_uid
            << ",\"asid\":" << row.asid << ",\"base\":" << row.base << ",\"bytes\":" << row.bytes
            << ",\"generation\":" << row.generation << ",\"virtual_us\":" << row.virtual_us
            << ",\"instructions\":" << row.instructions << ",\"rom\":" << (row.rom ? "true" : "false") << '}';
    }
    out << ']'; state->report = out.str(); return state->report.c_str();
}
EMSCRIPTEN_KEEPALIVE const void *eka2l1_js_code_data(unsigned index) {
    return state && !state->busy && index < state->mapped_code.size() ? state->mapped_code[index].code.data() : nullptr;
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
            const bool inspect = (state->capture || state->census) && state->code_witness == "null";
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
            cooperative_diagnostics::slice_census::token sample{};
            if (state->census) sample = state->census->begin(*state->system, common::benchmark::instructions.load());
            const bool running = state->system->loop();
            if (state->census) state->census->finish(sample, common::benchmark::instructions.load());
            if (!running) { state->running = false; break; }
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
    const bool diagnostics = state->capture || state->census;
    out << "{\"running\":" << (state->running ? "true" : "false")
        << ",\"virtual_us\":" << common::benchmark::virtual_us.load()
        << ",\"instructions\":" << common::benchmark::instructions.load()
        << ",\"presentations\":" << state->presentations
        << ",\"present_virtual_us\":" << state->present_virtual_us
        << ",\"present_instructions\":" << state->present_instructions
        << ",\"width\":" << state->width << ",\"height\":" << state->height
        << ",\"capture\":" << (state->capture ? "true" : "false")
        << ",\"code_witness\":" << state->code_witness
        << ",\"diagnostics\":" << (state->census ? "true" : "false")
        << ",\"heap\":";
    if (diagnostics) {
        const auto allocation = mallinfo();
        out << "{\"allocated\":" << allocation.uordblks
            << ",\"free\":" << allocation.fordblks << ",\"arena\":" << allocation.arena
            << ",\"end\":" << emscripten_get_heap_size()
            << ",\"break\":" << reinterpret_cast<std::uintptr_t>(sbrk(0)) << "}";
    } else {
        out << "null";
    }
    out << ",\"audio\":" << drivers::clocked_audio_stats() << "}";
    if (diagnostics && state->system->get_cpu()) {
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
#if defined(EKA2L1_COUNTED_REGION_JS)
    arm::region_js::reset();
#endif
#if defined(EKA2L1_COUNTED_LOOP_JS)
    arm::loop_js::reset();
#endif
#if defined(EKA2L1_COUNTED_JS)
    arm::counted_js::reset();
#endif
#if defined(EKA2L1_WORD_INCREMENT_JS)
    arm::word_increment::reset();
#endif
    state->graphics.reset();
    state->audio.reset();
    state.reset();
    return 0;
}
}
