#pragma once
#include <common/code_tracking_config.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace eka2l1::common::code_tracking {
    // Versions have one writer: the guest CPU. Host pointer escapes only set
    // escaped, permanently, before the pointer is returned to the caller.
    struct page_state {
        std::uint32_t version = 0;
        std::atomic<std::uint32_t> flags{0};
    };
    struct stamp {
        const page_state *page;
        std::uint32_t version;
        bool valid() const {
            return version && page->flags.load(std::memory_order_acquire) == 1
                && page->version == version;
        }
    };
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_VERSIONS)
    // Indexed by physical WASM backing page, so guest aliases share versions.
    // Stable storage: cached stamp pointers cannot dangle on unload/remapping.
    extern page_state pages[1u << 20];
    inline void guest_write(const void *ptr, std::size_t size) {
        if (!size) return;
        auto first = reinterpret_cast<std::uintptr_t>(ptr) >> 12;
        auto last = (reinterpret_cast<std::uintptr_t>(ptr) + size - 1) >> 12;
        for (auto p = first; p <= last; ++p) {
            auto &v = pages[p].version;
            if (v) ++v; // Overflow becomes zero: exact checking forever.
        }
    }
    void register_allocation(void *ptr, std::size_t size);
    void retire_allocation(void *ptr);
    void escape_pointer(const void *ptr);
    std::vector<stamp> snapshot(const void *ptr, std::size_t size);
#else
    inline void guest_write(const void *, std::size_t) {}
    inline void register_allocation(void *, std::size_t) {}
    inline void retire_allocation(void *) {}
    inline void escape_pointer(const void *) {}
    inline std::vector<stamp> snapshot(const void *, std::size_t) { return {}; }
#endif
}
