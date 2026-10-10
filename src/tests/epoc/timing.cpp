#include <catch2/catch.hpp>
#include <common/deterministic.h>
#include <kernel/timing.h>
#include <kernel/timer_deadline.h>

#include <cstdlib>
#include <vector>

namespace {
    struct benchmark_environment {
        benchmark_environment() {
#ifdef _WIN32
            _putenv_s("EKA2L1_BENCHMARK", "1");
#else
            setenv("EKA2L1_BENCHMARK", "1", 1);
#endif
        }
        ~benchmark_environment() {
#ifdef _WIN32
            _putenv_s("EKA2L1_BENCHMARK", "");
#else
            unsetenv("EKA2L1_BENCHMARK");
#endif
        }
    };
}

TEST_CASE("Virtual timer advances by instructions and skips idle time", "timing") {
    benchmark_environment environment;
    eka2l1::ntimer timer(2000000);
    timer.reset();
    std::vector<std::uint64_t> fired;
    const int evt = timer.register_event("test", [&](std::uint64_t data, int late) {
        REQUIRE(late == 0);
        fired.push_back(data);
    });
    timer.schedule_event(10, evt, 1);
    timer.schedule_event(10000000, evt, 2);
    timer.advance_instructions(19);
    REQUIRE(timer.microseconds() == 9);
    REQUIRE(fired.empty());
    timer.advance_instructions(1);
    REQUIRE(fired == std::vector<std::uint64_t>{1});
    REQUIRE(timer.advance_to_next_event());
    REQUIRE(timer.microseconds() == 10000000);
    REQUIRE(fired == std::vector<std::uint64_t>{1, 2});
    REQUIRE_FALSE(timer.advance_to_next_event());
}

TEST_CASE("Virtual timer supports cancellation and callbacks scheduling callbacks", "timing") {
    benchmark_environment environment;
    eka2l1::ntimer timer(1000000);
    timer.reset();
    int count = 0;
    int evt = timer.register_event("repeat", [&](std::uint64_t data, int) {
        ++count;
        if (data == 1) timer.schedule_event(5, timer.get_register_event("repeat"), 2);
    });
    timer.schedule_event(1, evt, 99);
    REQUIRE(timer.unschedule_event(evt, 99));
    timer.schedule_event(5, evt, 1);
    REQUIRE(timer.advance_to_next_event());
    REQUIRE(count == 1);
    REQUIRE(timer.advance_to_next_event());
    REQUIRE(count == 2);
    REQUIRE(timer.microseconds() == 10);
    timer.reset();
    REQUIRE(timer.microseconds() == 0);
    REQUIRE_FALSE(timer.advance_to_next_event());
}

TEST_CASE("Rounded User After leaves earlier events in order", "timing") {
    benchmark_environment environment;
    eka2l1::ntimer timer(1000000);
    timer.reset();
    timer.advance_instructions(10000);
    std::vector<std::uint64_t> fired;
    const int evt = timer.register_event("sleep ordering", [&](std::uint64_t data, int late) {
        REQUIRE(late == 0);
        fired.push_back(data);
    });
    const auto wakeup = eka2l1::kernel::user_after_deadline(timer.microseconds(), 1);
    timer.schedule_event_at(wakeup, evt, 2);
    timer.schedule_event_at(12000, evt, 1);
    REQUIRE(timer.advance_to_next_event(1000));
    REQUIRE(timer.microseconds() == 11000);
    REQUIRE(fired.empty());
    REQUIRE(timer.advance_to_next_event());
    REQUIRE(timer.microseconds() == 12000);
    REQUIRE(fired == std::vector<std::uint64_t>{1});
    REQUIRE(timer.advance_to_next_event());
    REQUIRE(timer.microseconds() == 15625);
    REQUIRE(fired == std::vector<std::uint64_t>{1, 2});
    REQUIRE_FALSE(timer.advance_to_next_event());
}

TEST_CASE("Paced timer delivers events without counting instructions", "timing") {
    benchmark_environment environment;
    eka2l1::ntimer timer(2000000);
    timer.reset();
    std::vector<std::uint64_t> fired;
    int evt = timer.register_event("paced", [&](std::uint64_t data, int late) {
        fired.push_back(data);
        if (data == 1) {
            REQUIRE(late == 5);
            timer.schedule_event(10, timer.get_register_event("paced"), 2);
        }
    });
    timer.schedule_event(10, evt, 1);
    timer.schedule_event(20, evt, 99);
    REQUIRE(timer.unschedule_event(evt, 99));
    timer.advance_host_clock(1000000);
    timer.advance_host_clock(1000009);
    REQUIRE(fired.empty());
    timer.advance_host_clock(1000015);
    REQUIRE(fired == std::vector<std::uint64_t>{1});
    timer.advance_host_clock(1000025);
    REQUIRE(fired == std::vector<std::uint64_t>{1, 2});
    REQUIRE(timer.microseconds() == 25);
    REQUIRE(eka2l1::common::benchmark::instructions.load() == 0);
}

TEST_CASE("Paced timer excludes pauses and drops long host stalls", "timing") {
    benchmark_environment environment;
    eka2l1::ntimer timer(2000000);
    timer.reset();
    timer.advance_host_clock(1000000);
    timer.advance_host_clock(1010000);
    REQUIRE(timer.microseconds() == 10000);
    timer.set_paused(true);
    timer.advance_host_clock(1020000);
    REQUIRE(timer.microseconds() == 10000);
    timer.set_paused(false);
    timer.advance_host_clock(1030000);
    timer.advance_host_clock(1040000);
    REQUIRE(timer.microseconds() == 20000);
    timer.advance_host_clock(7040000);
    REQUIRE(timer.microseconds() == 20000);
    timer.advance_host_clock(7050000);
    REQUIRE(timer.microseconds() == 30000);
    timer.reset();
    timer.advance_host_clock(7060000);
    REQUIRE(timer.microseconds() == 0);
    timer.advance_host_clock(7070000);
    REQUIRE(timer.microseconds() == 10000);
    REQUIRE(eka2l1::common::benchmark::instructions.load() == 0);
}
