#pragma once

#include <kernel/common.h>

#include <array>
#include <cstdint>

namespace eka2l1::hle {
    // Common slow and fast executive ordinals have dense, separate namespaces.
    // The map owns entries; its node addresses survive insertion and rehashing.
    class svc_registry {
        func_map entries_;
        std::array<const epoc_import_func *, 512> common_{};

        static std::uint32_t common_index(std::uint32_t ordinal) {
            if (ordinal < 256) return ordinal;
            if (ordinal - 0x800000u < 256) return 256 + ordinal - 0x800000u;
            return 512;
        }

    public:
        svc_registry() = default;
        svc_registry(const svc_registry &) = delete;
        svc_registry &operator=(const svc_registry &) = delete;

        template<typename Iterator>
        void insert(Iterator first, Iterator last) {
            for (; first != last; ++first) {
                const auto result = entries_.insert(*first);
                const auto index = common_index(first->first);
                if (index < common_.size()) common_[index] = &result.first->second;
            }
        }

        const epoc_import_func *find(std::uint32_t ordinal) const {
            const auto index = common_index(ordinal);
            if (index < common_.size()) return common_[index];
            const auto it = entries_.find(ordinal);
            return it == entries_.end() ? nullptr : &it->second;
        }

        void erase(std::uint32_t ordinal) {
            const auto index = common_index(ordinal);
            if (index < common_.size()) common_[index] = nullptr;
            entries_.erase(ordinal);
        }

        void clear() {
            common_.fill(nullptr);
            entries_.clear();
        }
    };
}
