#pragma once

#include <cpu/aot/aot_registry.h>
#include <cpu/arm_interface.h>
#include <cstring>
#include <deque>
#include <unordered_map>

namespace eka2l1::arm::aot {
    // RAM code may be patched through host pointers as well as guest stores.
    // Validate bytes on every entry; mapping notifications alone are insufficient.
    class validated_code_cache {
    public:
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
            auto it = current_.find(key(view.address_space, pc_mode));
            if (it == current_.end()) return nullptr;
            auto &entry = versions_[it->second];
            if (!view.bytes || view.bytes != entry.backing || view.size < entry.code.size()
                || std::memcmp(view.bytes, entry.code.data(), entry.code.size()) != 0) {
                entry.live = false;
                current_.erase(it);
                ++invalidations;
                return nullptr;
            }
            return &entry;
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
        // Versions remain allocated until reset so late module instantiation can
        // never attach an old function to a replacement block. Runtime caps growth.
        std::deque<block> versions_;
        std::unordered_map<std::uint64_t, std::uint32_t> current_;
    };
}
