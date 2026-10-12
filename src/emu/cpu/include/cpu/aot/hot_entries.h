#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace eka2l1::arm::aot {
    class rom_entry_counts {
        std::unordered_map<std::uint32_t, unsigned> counts_;
        std::size_t observed_ = 0;
        const std::size_t capacity_;
    public:
        explicit rom_entry_counts(std::size_t capacity = 65536) : capacity_(capacity) {}

        bool ready(std::uint32_t key, bool eager) {
            auto entry = counts_.find(key);
            if (entry == counts_.end()) {
                // Loaded-image preparation already has a bounded worklist.
                // Its entries must not exhaust runtime discovery admission.
                if (!eager && observed_ >= capacity_) return false;
                entry = counts_.emplace(key, 0).first;
                if (!eager) ++observed_;
            }
            auto &count = entry->second;
            if (count >= 8) return false; // One attempt per immutable entry.
            if (eager) count = 8;
            else ++count;
            return count == 8;
        }

        void clear() { counts_.clear(); observed_ = 0; }
    };
}
