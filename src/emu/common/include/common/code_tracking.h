#pragma once
#include <common/code_tracking_config.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace eka2l1::common::code_tracking {
    // Research mode is frozen before CPU startup; supported only in the
    // write-protection build. Interpreted/helper writes retain their barriers.
    inline bool protect_writes = false;
    // Executable-byte policy, frozen before CPU initialization. WASM trusts
    // loaded code by default; mode 0 restores mutation-compatible execution.
    // 1: trust instruction bytes; 2: omit code-write guards; 3: both, including
    // all mutation tracking. Mapping/lifetime invalidation remains independent.
#ifdef __EMSCRIPTEN__
    inline unsigned unsafe_code_mode = 3;
#else
    inline unsigned unsafe_code_mode = 0;
#endif
    inline bool skip_code_scans() { return (unsafe_code_mode & 1) != 0; }
    inline bool skip_code_write_guards() { return (unsafe_code_mode & 2) != 0; }
    inline bool skip_mutation_tracking() { return unsafe_code_mode == 3; }
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
#if defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
    // CPU-thread-only, as snapshot() and page versions already are. Zero is
    // permanently exhausted: every compiled entry must then rescan its TLB.
    extern std::uint64_t watch_generation;
    inline bool write_needs_callback(const void *ptr, std::size_t size) {
        if (skip_mutation_tracking() || !protect_writes || !ptr || !size) return false;
        const auto begin = reinterpret_cast<std::uintptr_t>(ptr);
        const auto end = std::uint64_t(begin) + size;
        if (end > (std::uint64_t(1) << 32)) return true;
        for (auto p=begin>>12;p<=(end-1)>>12;++p)
            if (pages[p].version && pages[p].flags.load(std::memory_order_acquire)==1) return true;
        return false;
    }
#endif
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
    // Guest writes and host escapes publish mutations. Only the CPU consumes
    // them; one epoch transition coalesces all writes since its last lookup.
    extern std::atomic<std::uint32_t> dirty;
    extern std::uint64_t epoch;
    inline std::uint64_t validation_epoch() {
        if (skip_mutation_tracking()) return 0;
        // CPU-thread only. Zero is permanently exhausted, never reused.
        if (dirty.load(std::memory_order_acquire)
            && dirty.exchange(0, std::memory_order_acq_rel) && epoch) ++epoch;
        return epoch;
    }
#endif
    inline void guest_write(const void *ptr, std::size_t size) {
        if (skip_mutation_tracking() || !size) return;
        auto first = reinterpret_cast<std::uintptr_t>(ptr) >> 12;
        auto last = (reinterpret_cast<std::uintptr_t>(ptr) + size - 1) >> 12;
        for (auto p = first; p <= last; ++p) {
            auto &v = pages[p].version;
            if (v) {
                ++v;
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
                if (!v) pages[p].flags.store(3, std::memory_order_release);
                dirty.store(1, std::memory_order_release);
#endif
            } // Overflow becomes zero: exact checking forever.
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
