#pragma once

#include <cstdint>
#include <string>

namespace eka2l1::arm::aot::memory_access_profile {
#ifdef EKA2L1_MEMORY_ACCESS_PROFILE
    // CPU-thread counters are read only while the route is paused.
    inline constexpr unsigned bucket_shift = 20, buckets = 4096, spaces = 256;
    extern std::uint64_t counters[spaces][buckets][2];
    extern std::uintptr_t active_row;
    inline void count(unsigned space, std::uint32_t address, bool write) {
        ++counters[space < spaces ? space : 0][address >> bucket_shift][write];
    }
    void process(unsigned space, const std::string &name);
    void event(const char *kind, unsigned space, std::uint32_t address,
        std::uint64_t size, std::uint64_t committed, bool local, bool direct);
    std::string report();
#endif
}
