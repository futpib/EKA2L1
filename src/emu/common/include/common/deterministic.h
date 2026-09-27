#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace eka2l1::common::benchmark {
    // Opt-in only. Set before creating the emulator (or before WASM init).
    inline bool enabled() {
        const char *value = std::getenv("EKA2L1_BENCHMARK");
        return value && std::strcmp(value, "1") == 0;
    }

    // Configured before guest threads start. Live play retains this clock but
    // accepts queued input and does not retain benchmark artifacts.
    inline bool interactive = false;
    inline std::atomic<std::uint64_t> virtual_us{0};
    inline std::atomic<std::uint64_t> instructions{0};
    inline constexpr std::uint64_t epoch_us = 1704067200000000ULL; // 2024-01-01 UTC

    inline int frame_count() {
        const char *value = std::getenv("EKA2L1_BENCHMARK_FRAMES");
        return value ? std::atoi(value) : 600;
    }
}
