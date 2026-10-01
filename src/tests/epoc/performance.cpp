#include <catch2/catch.hpp>
#include <common/performance.h>
#include <future>
#include <thread>

TEST_CASE("Performance window pauses before measurement and stops at guest deadline", "performance") {
    namespace perf = eka2l1::common::performance;
    perf::enabled = true;
    perf::start_us = 100;
    perf::end_us = 200;
    perf::phase = 0;
    REQUIRE_FALSE(perf::checkpoint(99, 1000));
    auto guest = std::async(std::launch::async, [] { return perf::checkpoint(100, 1001); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (perf::phase.load() != 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    const bool paused = perf::phase.load() == 1;
    // Always release before assertions so a failed check cannot strand a thread.
    perf::resume();
    REQUIRE(paused);
    REQUIRE_FALSE(guest.get());
    REQUIRE(perf::first_us == 100);
    REQUIRE(perf::first_instructions == 1001);
    REQUIRE(perf::phase.load() == 2);
    REQUIRE_FALSE(perf::checkpoint(199, 2000));
    REQUIRE(perf::checkpoint(200, 2001));
    REQUIRE(perf::last_us == 200);
    REQUIRE(perf::last_instructions == 2001);
    REQUIRE(perf::phase.load() == 3);
    perf::enabled = false;
    perf::phase = 0;
}

TEST_CASE("Wall timing can exclude detailed instrumentation", "performance") {
    namespace perf = eka2l1::common::performance;
    perf::enabled = true; perf::detailed = false; perf::phase = 2;
    const auto before = perf::counters[perf::cpu_run].calls.load();
    { perf::scope scope(perf::cpu_run); REQUIRE_FALSE(perf::counting()); }
    REQUIRE(perf::counters[perf::cpu_run].calls.load() == before);
    perf::enabled = false; perf::detailed = true; perf::phase = 0;
}
