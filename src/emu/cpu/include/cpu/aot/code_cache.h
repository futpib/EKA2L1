#pragma once

#include <cpu/aot/aot_registry.h>
#include <cpu/arm_interface.h>
#include <cstring>
#include <array>
#include <deque>
#include <unordered_map>

namespace eka2l1::arm::aot {
    bool equal_code_bytes(const std::uint8_t *a, const std::uint8_t *b, std::size_t size);

    // RAM code may be patched through host pointers as well as guest stores.
    // Validate bytes on every entry; mapping notifications alone are insufficient.
    class validated_code_cache {
    public:
        validated_code_cache() = default;
        validated_code_cache(const validated_code_cache &) = delete;
        validated_code_cache &operator=(const validated_code_cache &) = delete;
        validated_code_cache(validated_code_cache &&) = default;
        validated_code_cache &operator=(validated_code_cache &&) = default;

        struct block {
            std::uint64_t key;
            std::uint32_t version;
            const std::uint8_t *backing;
            std::vector<std::uint8_t> code;
            aot_func function = nullptr;
            bool live = true;
            bool rejected = false;
        };

        static std::uint64_t key(std::uint32_t space, std::uint32_t pc_mode) {
            return (std::uint64_t(space) << 32) | pc_mode;
        }

        block *find(std::uint32_t pc_mode, const core::code_mapping &view) {
            const auto k = key(view.address_space, pc_mode);
            auto &recent = recent_[recent_index(k)];
            block *entry = recent;
            if (!entry || !entry->live || entry->key != k) {
                auto it = current_.find(k);
                if (it == current_.end()) return nullptr;
                entry = &versions_[it->second];
                recent = entry;
            }
            // A recent hit only skips the container search. Resolve the mapping
            // at the caller and check the backing, extent and exact bytes every
            // time, including after aliased/host writes and address-space reuse.
            if (!view.bytes || view.bytes != entry->backing || view.size < entry->code.size()
                || !equal_code_bytes(view.bytes, entry->code.data(), entry->code.size())) {
                entry->live = false;
                current_.erase(k);
                recent = nullptr;
                ++invalidations;
                return nullptr;
            }
            return entry;
        }

        block &insert(std::uint32_t pc_mode, const core::code_mapping &view, std::size_t size) {
            const auto k = key(view.address_space, pc_mode);
            auto old = current_.find(k);
            if (old != current_.end()) versions_[old->second].live = false;
            const auto version = static_cast<std::uint32_t>(versions_.size());
            versions_.push_back({k, version, view.bytes, {view.bytes, view.bytes + size}});
            current_[k] = version;
            return versions_.back();
        }

        void attach(std::uint32_t version, aot_func function) {
            if (version < versions_.size() && versions_[version].live)
                versions_[version].function = function;
        }

        void invalidate(std::uint32_t address, std::size_t size) {
            const std::uint64_t end = std::uint64_t(address) + size;
            for (auto it = current_.begin(); it != current_.end();) {
                auto &entry = versions_[it->second];
                const auto start = static_cast<std::uint32_t>(entry.key) & ~1u;
                if (start < end && std::uint64_t(start) + entry.code.size() > address) {
                    entry.live = false;
                    it = current_.erase(it);
                    ++invalidations;
                } else ++it;
            }
        }

        std::size_t versions() const { return versions_.size(); }
        std::uint64_t invalidations = 0;

    private:
        static std::size_t recent_index(std::uint64_t k) {
            const auto pc_mode = static_cast<std::uint32_t>(k);
            return ((pc_mode >> 1) ^ (pc_mode << 7) ^ (k >> 32)) & 4095;
        }
        // deque entries stay allocated until reset; invalidation/replacement
        // marks old entries dead before a cached pointer can be reused.
        std::array<block *, 4096> recent_{};
        // Versions remain allocated until reset so late module instantiation can
        // never attach an old function to a replacement block. Runtime caps growth.
        std::deque<block> versions_;
        std::unordered_map<std::uint64_t, std::uint32_t> current_;
    };
}
