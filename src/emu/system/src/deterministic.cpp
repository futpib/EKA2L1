#include <system/deterministic.h>
#include <system/epoc.h>
#include <common/deterministic.h>
#include <common/performance.h>
#include <common/log.h>
#include <kernel/timing.h>
#include <services/window/window.h>
#include <drivers/input/common.h>
#include <drivers/audio/deterministic.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace eka2l1 {
    void start_benchmark_input(system *sys, window_server *winserv) {
        if (!common::benchmark::enabled()) return;
        if (!winserv) throw std::runtime_error("Benchmark requires a window server");
        auto *timer = sys->get_ntimer();
        drivers::reset_benchmark_audio();
        const int audio_event = timer->register_event("BenchmarkAudio", [timer](std::uint64_t deadline, int) {
            common::performance::scope audio_scope(common::performance::audio);
            drivers::pump_benchmark_audio(timer->microseconds());
            const auto next = deadline + 10000;
            timer->schedule_event(static_cast<std::int64_t>(next) - timer->microseconds(),
                timer->get_register_event("BenchmarkAudio"), next);
        });
        timer->schedule_event(10000, audio_event, timer->microseconds() + 10000);
        if (common::benchmark::interactive) return;
        const char *path = std::getenv("EKA2L1_BENCHMARK_INPUT");
        if (!path) throw std::runtime_error("Set EKA2L1_BENCHMARK_INPUT to a replay file");
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot open benchmark input replay");
        const int event = timer->register_event("BenchmarkInput", [winserv](std::uint64_t data, int) {
            drivers::input_event input{};
            input.type_ = drivers::input_event_type::key_raw;
            input.key_.code_ = static_cast<int>(data >> 1);
            input.key_.state_ = (data & 1) ? drivers::key_state::pressed : drivers::key_state::released;
            winserv->queue_input_from_driver(input);
        });
        std::string line;
        std::uint64_t previous = 0;
        while (std::getline(input, line)) {
            if (line.empty() || line.front() == '#') continue;
            std::istringstream row(line);
            std::uint64_t us;
            unsigned key, down;
            std::string extra;
            if (!(row >> us >> key >> down) || (row >> extra) || us < previous || us > 120000000 || key > 255 || down > 1)
                throw std::runtime_error("Invalid benchmark replay row: " + line);
            timer->schedule_event(us, event, (static_cast<std::uint64_t>(key) << 1) | down);
            previous = us;
        }
        const int limit = timer->register_event("BenchmarkLimit", [](std::uint64_t, int) {
            LOG_ERROR(FRONTEND_CMDLINE, "Benchmark exceeded its virtual-time limit");
            std::_Exit(2);
        });
        const std::uint64_t limit_us = !common::benchmark::retain_audio && common::performance::enabled
            ? std::max(std::uint64_t(120000000), common::performance::end_us + 1000000)
            : 120000000;
        timer->schedule_event(limit_us, limit, 0);
    }
}
