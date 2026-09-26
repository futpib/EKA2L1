#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <sstream>

namespace eka2l1::common::performance {
    enum category { cpu_loop, cpu_run, timers, scheduler, graphics_dispatch,
        graphics_wait, display_hook, readback, frame_capture, png_encode, audio, category_count };
    inline const char *names[] = {"cpu_loop", "cpu_run", "timers", "scheduler", "graphics_dispatch",
        "graphics_wait", "display_hook", "readback", "frame_capture", "png_encode", "audio"};
    struct counter {
        std::atomic<std::uint64_t> ns{0};
        std::atomic<std::uint64_t> calls{0};
    };
    inline counter counters[category_count];
    inline bool detailed = true;
    inline bool enabled = false; // Configured before starting guest threads.
    inline int capture_mode = 0; // 0: full, 1: no PNG encoding, 2: no readback/capture.
    inline std::uint64_t start_us = 0, end_us = 0;
    inline std::atomic<int> phase{0}; // warmup, paused for profiler, running, done
    inline std::mutex pause_mutex;
    inline std::condition_variable pause_condition;
    inline std::uint64_t first_us = 0, last_us = 0, first_instructions = 0, last_instructions = 0;
    inline std::atomic<std::uint64_t> presentations{0};
    // These counters are written only by the guest CPU thread and read after
    // phase 3's release/acquire handoff. No atomics in the instruction hot path.
    inline std::uint64_t aot_dispatches = 0, aot_instructions = 0;
    inline std::uint64_t ram_aot_dispatches = 0, ram_blocks_compiled = 0, compiled_runner_calls = 0;
    inline std::uint64_t decoded_instructions = 0, cache_hits = 0, cache_misses = 0;
    inline std::uint64_t cache_clears = 0, context_loads = 0, imb_calls = 0;
    inline bool counting() { return enabled && detailed && phase.load(std::memory_order_relaxed) == 2; }
    inline std::chrono::steady_clock::time_point begin;
    inline double wall_seconds = 0;

    struct scope {
        category id;
        bool active;
        std::chrono::steady_clock::time_point start;
        explicit scope(category value) : id(value), active(enabled && detailed && phase.load(std::memory_order_relaxed) == 2) {
            if (active) start = std::chrono::steady_clock::now();
        }
        ~scope() {
            if (!active) return;
            counters[id].ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed);
            counters[id].calls.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Called between guest dispatches. Pausing lets host profilers attach without
    // advancing guest time. Wall time is observed only, never fed to emulation.
    inline bool checkpoint(std::uint64_t us, std::uint64_t instructions) {
        if (!enabled) return false;
        if (phase.load() == 0 && us >= start_us) {
            std::unique_lock<std::mutex> lock(pause_mutex);
            first_us = us;
            first_instructions = instructions;
            phase = 1;
            pause_condition.wait(lock, [] { return phase.load() != 1; });
            begin = std::chrono::steady_clock::now();
        }
        if (phase.load() == 2 && us >= end_us) {
            wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            last_us = us;
            last_instructions = instructions;
            phase = 3;
            return true;
        }
        return false;
    }

    inline void resume() {
        std::lock_guard<std::mutex> lock(pause_mutex);
        if (phase.load() == 1) {
            phase = 2;
            pause_condition.notify_one();
        }
    }

    inline std::string report() {
        std::ostringstream out;
        out << "{\"detailed\":" << (detailed ? "true" : "false") << ",\"capture_mode\":" << capture_mode << ",\"first_virtual_us\":" << first_us
            << ",\"last_virtual_us\":" << last_us << ",\"first_instructions\":" << first_instructions
            << ",\"last_instructions\":" << last_instructions << ",\"wall_seconds\":" << wall_seconds
            << ",\"presentations\":" << presentations.load()
            << ",\"aot_dispatches\":" << aot_dispatches << ",\"aot_instructions\":" << aot_instructions
            << ",\"compiled_runner_calls\":" << compiled_runner_calls
            << ",\"ram_aot_dispatches\":" << ram_aot_dispatches << ",\"ram_blocks_compiled\":" << ram_blocks_compiled
            << ",\"decoded_instructions\":" << decoded_instructions
            << ",\"cache_hits\":" << cache_hits << ",\"cache_misses\":" << cache_misses
            << ",\"cache_clears\":" << cache_clears << ",\"context_loads\":" << context_loads
            << ",\"imb_calls\":" << imb_calls << ",\"scopes\":{";
        for (int i = 0; i < category_count; ++i) {
            if (i) out << ',';
            out << '"' << names[i] << "\":{\"seconds\":" << counters[i].ns.load() / 1e9
                << ",\"calls\":" << counters[i].calls.load() << '}';
        }
        out << "}}";
        return out.str();
    }
}
