#include <common/code_tracking.h>
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_VERSIONS)
#include <map>
#include <mutex>
#include <limits>

namespace eka2l1::common::code_tracking {
    static_assert(sizeof(page_state) == 8);
    page_state pages[1u << 20];
#if defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
    std::uint64_t watch_generation = 1;
#endif
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
    std::atomic<std::uint32_t> dirty{0};
    std::uint64_t epoch = 1;
#endif
    static std::mutex mutex;
    static std::map<std::uintptr_t, std::uintptr_t> allocations;

    void register_allocation(void *ptr, std::size_t size) {
        if (!ptr || !size) return;
        const auto begin = reinterpret_cast<std::uintptr_t>(ptr);
        const auto end = std::uint64_t(begin) + size;
        if (end > (std::uint64_t(1) << 32)) return;
        std::lock_guard<std::mutex> lock(mutex);
        allocations[begin] = end;
        for (auto p = begin >> 12; p <= (end - 1) >> 12; ++p) {
            // Never rehabilitate escaped/reused backing. This is conservative
            // even when allocations share a host page or a pointer was retained.
            auto &entry = pages[p];
            if (entry.flags.load()) entry.flags.store(3, std::memory_order_release);
            else {
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
                entry.version = 0; // Loader/data writes do not watch code yet.
#else
                entry.version = 1;
#endif
                entry.flags.store(1, std::memory_order_release);
            }
        }
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
        dirty.store(1, std::memory_order_release); // Includes overlapping reuse.
#endif
    }
    void escape_pointer(const void *ptr) {
        const auto address = reinterpret_cast<std::uintptr_t>(ptr);
        if (!ptr || !pages[address >> 12].flags.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lock(mutex);
        // A raw pointer has no lifetime or extent. Poison the entire allocation,
        // not just its first page: callers may retain it and perform arithmetic.
        for (const auto &[begin, end] : allocations) {
            if ((address >> 12) < (begin >> 12) || (address >> 12) > ((end - 1) >> 12)) continue;
            for (auto p = begin >> 12; p <= (end - 1) >> 12; ++p)
                pages[p].flags.store(3, std::memory_order_release);
        }
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
        dirty.store(1, std::memory_order_release);
#endif
    }
    void retire_allocation(void *ptr) {
        escape_pointer(ptr);
        std::lock_guard<std::mutex> lock(mutex);
        allocations.erase(reinterpret_cast<std::uintptr_t>(ptr));
    }
    std::vector<stamp> snapshot(const void *ptr, std::size_t size) {
        std::vector<stamp> result;
        if (!ptr || !size) return result;
        const auto begin = reinterpret_cast<std::uintptr_t>(ptr);
        const auto end = std::uint64_t(begin) + size;
        if (end > (std::uint64_t(1) << 32)) return result;
        for (auto p = begin >> 12; p <= (end - 1) >> 12; ++p) {
            auto &entry = pages[p];
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
            if (entry.flags.load(std::memory_order_acquire) != 1) return {};
            // CPU-thread only, after comparing the snapshot's source bytes.
            if (!entry.version) {
                entry.version = 1;
#if defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
                if (watch_generation) ++watch_generation;
#endif
            }
#endif
            if (!entry.version || entry.flags.load(std::memory_order_acquire) != 1) {
                return {};
            }
            result.push_back({&entry, entry.version});
        }
        return result;
    }
}
#endif
