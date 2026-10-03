#pragma once

#include <cpu/aot/aot_registry.h>
#include <cpu/aot/exit_census.h>
#include <common/code_tracking.h>
#include <common/performance.h>
#include <cpu/arm_interface.h>
#include <cstring>
#include <array>
#include <algorithm>
#include <deque>
#include <unordered_map>

namespace eka2l1::arm::aot {
    extern unsigned code_compare_mode; // 0: original, 1: overlapping tail, 2: four-vector loop, 3: fixed short sizes + grouped, 4: stored comparator
    bool equal_code_bytes(const std::uint8_t *a, const std::uint8_t *b, std::size_t size);
    using code_comparator = bool (*)(const std::uint8_t *, const std::uint8_t *, std::size_t);
    code_comparator select_code_comparator(std::size_t size);

    // Mapping/lifetime checks apply in every mode. Instruction-byte validation
    // follows the selected executable-byte policy.
    class validated_code_cache {
    public:
        validated_code_cache() = default;
        validated_code_cache(const validated_code_cache &) = delete;
        validated_code_cache &operator=(const validated_code_cache &) = delete;
        validated_code_cache(validated_code_cache &&) = default;
        validated_code_cache &operator=(validated_code_cache &&) = default;

        struct dependency {
            std::uint32_t address;
            const std::uint8_t *backing;
            std::vector<std::uint8_t> code;
            code_comparator comparator = equal_code_bytes;
        };
        struct block {
            std::uint64_t key;
            std::uint32_t version;
            const std::uint8_t *backing;
            std::vector<std::uint8_t> code;
            std::vector<dependency> dependencies;
            std::uintptr_t guard_begin = 0, guard_end = 0;
            aot_func function = nullptr;
            bool live = true;
            bool rejected = false;
            std::uint64_t mapping_generation = 0;
            const std::atomic<std::uint64_t> *mapping_source = nullptr;
            // Snapshot length stays fixed until this version is discarded.
            code_comparator comparator = equal_code_bytes;
        };

        static std::uint64_t key(std::uint32_t space, std::uint32_t pc_mode) {
            return (std::uint64_t(space) << 32) | pc_mode;
        }

        block *find(std::uint32_t pc_mode, const core::code_mapping &view, core *cpu = nullptr) {
            return find_mapped<false>(pc_mode, view, cpu);
        }
        // Called only after the outer boundary selected trusted bytes.
        // Mapping/lifetime checks still apply on every lookup.
        block *find_trusted_original(std::uint32_t pc_mode, core &cpu);

    private:
        template<bool TrustBytes>
        block *find_mapped(std::uint32_t pc_mode, const core::code_mapping &view, core *cpu) {
            const auto k = key(view.address_space, pc_mode);
            auto &recent = recent_[recent_index(k)];
            block *entry = recent;
            if (!entry || !entry->live || entry->key != k) {
                auto it = current_.find(k);
                if (it == current_.end()) return nullptr;
                entry = &versions_[it->second];
                recent = entry;
            }
            // Refresh every dependency before dereferencing cached backing.
            // A generation change may have freed any of these mappings.
            bool dependencies_mapped = true;
            for (const auto &d : entry->dependencies) {
                core::code_mapping mapped;
                if (!cpu || !cpu->resolve_code || !cpu->resolve_code(d.address, mapped)
                    || mapped.address_space != view.address_space || mapped.bytes != d.backing
                    || mapped.size < d.code.size()) { dependencies_mapped = false; break; }
            }
            // A recent hit only skips the container search. Resolve the mapping
            // at the caller and check backing and extent after mapping changes,
            // including address-space reuse. Byte checks follow the active policy.
            const bool mapping_invalid = !dependencies_mapped || !view.bytes
                || view.bytes != entry->backing || view.size < entry->code.size();
            if (mapping_invalid || (!TrustBytes && !bytes_match(*entry))) {
                if(exit_census::counting())++exit_census::invalidations[mapping_invalid?"mapping_or_extent":"exact_bytes"];
                entry->live = false;
                current_.erase(k);
                recent = nullptr;
                ++invalidations;
                return nullptr;
            }
            return entry;
        }

    public:
#if defined(_MSC_VER)
        __forceinline
#else
        __attribute__((always_inline))
#endif
        block *find(std::uint32_t pc_mode, core &cpu) {
            return find_original(pc_mode, cpu);
        }

    private:
        // Container recovery, mapping refresh and invalidation stay outside
        // the small recent-hit path. Definitions live in code_compare.cpp.
        template<bool TrustBytes>
        block *find_original_impl(std::uint32_t pc_mode, core &cpu);
        block *find_original(std::uint32_t pc_mode, core &cpu);

    public:
        block &insert(std::uint32_t pc_mode, const core::code_mapping &view, std::size_t size) {
            const auto k = key(view.address_space, pc_mode);
            auto old = current_.find(k);
            if (old != current_.end()) versions_[old->second].live = false;
            const auto version = static_cast<std::uint32_t>(versions_.size());
            versions_.push_back({k, version, view.bytes, {view.bytes, view.bytes + size}});
            current_[k] = version;
            auto &entry = versions_.back();
            if (code_compare_mode == 4) entry.comparator = select_code_comparator(size);
            entry.guard_begin = reinterpret_cast<std::uintptr_t>(view.bytes);
            entry.guard_end = entry.guard_begin + size;
            return entry;
        }

        static void add_dependency(block &entry, std::uint32_t address,
            const std::uint8_t *backing, const std::vector<std::uint8_t> &bytes) {
            entry.dependencies.push_back({address, backing, bytes,
                code_compare_mode == 4 ? select_code_comparator(bytes.size()) : equal_code_bytes});
            const auto begin = reinterpret_cast<std::uintptr_t>(backing);
            entry.guard_begin = std::min(entry.guard_begin, begin);
            entry.guard_end = std::max(entry.guard_end, begin + bytes.size());
        }

        static bool diagnostic_code_overlap(const block &entry, std::uintptr_t host, std::size_t size) {
            const auto overlaps=[&](const std::uint8_t *backing,std::size_t bytes) {
                const auto begin=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(backing));
                const auto start=static_cast<std::uint64_t>(host);
                return size && bytes && start < begin+bytes && begin < start+size;
            };
            if(overlaps(entry.backing,entry.code.size()))return true;
            for(const auto &dependency:entry.dependencies)if(overlaps(dependency.backing,dependency.code.size()))return true;
            return false;
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
                const bool dependency_hit = std::any_of(entry.dependencies.begin(), entry.dependencies.end(),
                    [&](const auto &d) { return d.address < end && std::uint64_t(d.address) + d.code.size() > address; });
                if (dependency_hit || (start < end && std::uint64_t(start) + entry.code.size() > address)) {
                    entry.live = false;
                    it = current_.erase(it);
                    ++invalidations;
                } else ++it;
            }
        }

        std::size_t versions() const { return versions_.size(); }
        std::uint64_t invalidations = 0;

    private:
        template <typename Snapshot>
        static bool snapshot_equal(const Snapshot &snapshot) {
            if (code_compare_mode == 4)
                return snapshot.comparator(snapshot.backing, snapshot.code.data(), snapshot.code.size());
            return equal_code_bytes(snapshot.backing, snapshot.code.data(), snapshot.code.size());
        }
        static bool bytes_match(block &entry) {
            if (common::code_tracking::skip_code_scans()) return true;
            return snapshot_equal(entry) && dependencies_equal(entry);
        }
        static bool dependencies_equal(const block &entry) {
            for (const auto &d : entry.dependencies)
                if (!snapshot_equal(d)) return false;
            return true;
        }
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
