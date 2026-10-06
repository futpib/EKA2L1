#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace eka2l1::arm::aot::memory_experiment {
    // Frozen before guest initialization: WASM defaults to direct memory (2).
    // Native cores retain TLB (0); direct host pointers address WASM memory.
#ifdef __EMSCRIPTEN__
    inline unsigned mode = 2;
#else
    inline unsigned mode = 0;
#endif
    inline bool enabled() { return mode == 2; }
    struct statistics {
        std::uint64_t rebuilds = 0, mapped_pages = 0;
        std::uint64_t arena_bytes = 0, direct_pages = 0, direct_rebuilds = 0;
    };
    inline statistics stats;
    struct binding { std::uint32_t guest, host, permissions; };
    struct range { std::uint32_t begin, size, host, permissions; };
    struct page { std::uint32_t read, write; };
    // A bounded local-address arena shares the runtime's primary WASM memory.
    // Other addresses use the page table, including physical aliases.
    inline constexpr std::uint32_t direct_begin = 0x00400000, direct_size = 64 * 1024 * 1024;
    struct direct_view {
        std::uint32_t begin, size, host, pages;
        std::uint32_t bias = 0, arena_mask = 0;
    };

    class view {
        std::uint64_t generation_ = 0;
        std::uint32_t space_ = ~0u;
        std::vector<binding> bindings_;
        std::vector<page> pages_;
        direct_view direct_{};
        bool active_ = false;
    public:
        std::uintptr_t enter(std::uint64_t generation, std::uint32_t space,
            const std::function<std::vector<binding>()> &resolve, range direct = {});
        void leave();
    };
}
