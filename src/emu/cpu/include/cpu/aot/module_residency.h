#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace eka2l1::arm::aot {
    // Tracks table ownership, not guest-code validity. Replacing the last
    // export releases a module; table and module indices are both reusable.
    class module_residency {
        struct module {
            std::size_t bytes = 0, live = 0;
            std::vector<std::uintptr_t> slots;
        };
        std::vector<module> modules_{{}};
        std::vector<std::size_t> owners_;
        std::vector<std::uint64_t> used_;
        std::vector<std::uint8_t> heat_;
        std::vector<std::size_t> free_;
        std::uint64_t clock_ = 0;
        std::size_t bytes_ = 0;

    public:
        std::size_t bytes() const { return bytes_; }

        void release(std::uintptr_t slot) {
            if (slot >= owners_.size() || !owners_[slot]) return;
            const auto owner = std::exchange(owners_[slot], 0);
            auto &entry = modules_[owner];
            if (--entry.live) return;
            bytes_ -= entry.bytes;
            entry = {};
            free_.push_back(owner);
        }

        void install(std::size_t bytes, const std::vector<std::uintptr_t> &slots) {
            if (slots.empty()) return;
            std::vector<std::uint64_t> usage;
            std::vector<std::uint8_t> heat;
            for (auto slot : slots) {
                usage.push_back(slot < owners_.size() && owners_[slot] ? used_[slot] : 0);
                heat.push_back(slot < owners_.size() && owners_[slot] ? heat_[slot] : 0);
                release(slot);
            }
            const auto owner = free_.empty() ? modules_.size() : free_.back();
            if (free_.empty()) modules_.emplace_back();
            else free_.pop_back();
            modules_[owner] = {bytes, slots.size(), slots};
            bytes_ += bytes;
            for (std::size_t i = 0; i < slots.size(); ++i) {
                const auto slot = slots[i];
                if (slot >= owners_.size()) {
                    owners_.resize(slot + 1);
                    used_.resize(slot + 1);
                    heat_.resize(slot + 1);
                }
                owners_[slot] = owner;
                used_[slot] = usage[i];
                heat_[slot] = heat[i];
            }
            ++clock_;
        }

        void touch(std::uintptr_t slot) {
            // One observation per installation epoch protects recurring code
            // from startup scans without counting executions or instructions.
            if (slot < used_.size() && used_[slot] != clock_) {
                used_[slot] = clock_;
                if (heat_[slot] < 15) ++heat_[slot];
            }
        }

        std::vector<std::uintptr_t> victims(std::size_t target) {
            std::vector<std::size_t> ordered;
            std::vector<std::uint64_t> recent(modules_.size());
            std::vector<std::uint8_t> heat(modules_.size());
            for (std::size_t i = 1; i < modules_.size(); ++i)
                if (modules_[i].live) {
                    ordered.push_back(i);
                    for (auto slot : modules_[i].slots)
                        if (owners_[slot] == i) {
                            recent[i] = std::max(recent[i], used_[slot]);
                            // Installation advances even without a trim. Let
                            // completed startup phases lose their old priority.
                            const auto age = std::min<std::uint64_t>(4, (clock_ - used_[slot]) / 256);
                            heat[i] = std::max(heat[i], static_cast<std::uint8_t>(heat_[slot] >> age));
                        }
                }
            std::sort(ordered.begin(), ordered.end(), [&](auto a, auto b) {
                return heat[a] != heat[b] ? heat[a] < heat[b] : recent[a] < recent[b];
            });
            auto bytes = bytes_;
            std::vector<std::uintptr_t> slots;
            for (auto owner : ordered) {
                if (bytes <= target) break;
                const auto &entry = modules_[owner];
                for (auto slot : entry.slots)
                    if (owners_[slot] == owner) slots.push_back(slot);
                bytes -= entry.bytes;
            }
            // A formerly hot startup loop must eventually become evictable.
            for (auto &value : heat_) value >>= 1;
            return slots;
        }
    };
}
