#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace eka2l1::arm::aot::memory_experiment {
    // Frozen before guest initialization. Zero keeps the production TLB path.
    inline unsigned mode = 0;
    inline std::uint64_t activation_us = 0, activated_us = 0;
    inline bool identity_active = true;
    inline bool enabled(){return mode && (mode!=2 || identity_active);}
    void activate_identity();
    // The ROM loader supplies backing bounds; its guest permission bits are RWX.
    inline std::uintptr_t immutable_rom_begin = 0, immutable_rom_end = 0;
    struct statistics {
        std::uint64_t rebuilds = 0, chains = 0, instructions = 0;
        std::uint64_t bytes_in = 0, bytes_out = 0, alias_pages = 0;
        std::uint64_t mapped_pages = 0, ranges = 0, largest_range = 0;
    };
    inline statistics stats;
    struct binding { std::uint32_t guest, host, permissions; };
    struct range { std::uint32_t begin, size, host, permissions; };
    struct page { std::uint32_t read, write; };
    struct identity_view { std::uint32_t aliases, dirty; };

    // The second memory belongs to the CPU worker. Copies are real WASM calls,
    // with no JS transition after initialization.
    void initialize_identity_memory();
    void copy_to_identity(std::uint32_t guest, std::uint32_t host, std::uint32_t size);
    void copy_from_identity(std::uint32_t host, std::uint32_t guest, std::uint32_t size);

    class view {
        std::uint64_t generation_ = 0;
        std::uint32_t space_ = ~0u;
        std::vector<binding> bindings_;
        std::vector<range> ranges_, range_pages_, refresh_ranges_;
        std::vector<page> pages_;
        std::vector<std::uint32_t> aliases_;
        std::vector<std::uint8_t> dirty_;
        identity_view identity_{};
        bool active_ = false;
    public:
        std::uintptr_t enter(std::uint64_t generation, std::uint32_t space,
            const std::function<std::vector<binding>()> &resolve);
        void leave();
    };
}
