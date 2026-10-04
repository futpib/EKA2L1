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
        std::uint64_t arena_bytes = 0, direct_pages = 0, direct_rebuilds = 0;
    };
    inline statistics stats;
    struct binding { std::uint32_t guest, host, permissions; };
    struct range { std::uint32_t begin, size, host, permissions; };
    struct page { std::uint32_t read, write; };
    // A bounded local-address arena shares the runtime's primary WASM memory.
    // Other addresses use the page table, including physical aliases.
    inline constexpr std::uint32_t direct_begin = 0x00400000, direct_size = 64 * 1024 * 1024;
    struct direct_view { std::uint32_t begin, size, host, pages; };

    class view {
        std::uint64_t generation_ = 0;
        std::uint32_t space_ = ~0u;
        std::vector<binding> bindings_;
        std::vector<range> ranges_, range_pages_;
        std::vector<page> pages_;
        direct_view direct_{};
        bool active_ = false;
    public:
        std::uintptr_t enter(std::uint64_t generation, std::uint32_t space,
            const std::function<std::vector<binding>()> &resolve, range direct = {});
        void leave();
    };
}
