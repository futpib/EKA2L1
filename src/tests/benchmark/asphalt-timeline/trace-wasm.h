#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <pthread.h>
#include <common/deterministic.h>
namespace eka_trace {
    inline constexpr unsigned capacity = 262144;
    struct row { std::uint64_t host, guest, kind, thread, a, b, c, d; };
    inline row rows[capacity];
    inline std::atomic<unsigned> enabled{0}, count{0};
    inline std::uint64_t now() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    inline void event(unsigned kind, std::uint64_t a=0, std::uint64_t b=0, std::uint64_t c=0, std::uint64_t d=0) {
        if (!enabled.load(std::memory_order_relaxed)) return;
        const auto host=now();
        const auto slot=count.fetch_add(1,std::memory_order_relaxed);
        if(slot<capacity) rows[slot]={host,eka2l1::common::benchmark::virtual_us.load(),kind,(std::uintptr_t)pthread_self(),a,b,c,d};
    }
}
