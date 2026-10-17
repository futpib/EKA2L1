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
#include <map>
#include <memory>
#include <unordered_map>

namespace eka2l1::arm::aot {
    bool equal_code_bytes(const std::uint8_t *a, const std::uint8_t *b, std::size_t size);

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
            bool unloaded = false;
            std::uint64_t mapping_generation = 0;
            const std::atomic<std::uint64_t> *mapping_source = nullptr;
            unsigned policy = 0;
            std::shared_ptr<unsigned char> function_lifetime;
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

        // Dispatch consumes only the function. Keep its validated identity in
        // one contiguous slot instead of chasing the version's cold metadata.
#if defined(_MSC_VER)
        __forceinline
#else
        __attribute__((always_inline))
#endif
        aot_func lookup_trusted(std::uint32_t pc_mode, core &cpu) {
            const auto *source = cpu.code_mapping_generation;
            const auto generation = source ? source->load(std::memory_order_acquire) : 0;
            const auto k = key(cpu.code_address_space, pc_mode);
            auto &slot = dispatch_[recent_index(k)];
            if (generation && slot.key == k && slot.generation == generation
                && slot.source == source && slot.function) return slot.function;
            return lookup_trusted_uncached(pc_mode, cpu);
        }

    private:
        aot_func lookup_trusted_uncached(std::uint32_t pc_mode, core &cpu);
        void forget_dispatch(std::uint64_t k) {
            auto &slot = dispatch_[recent_index(k)];
            if (slot.key == k) slot = {};
        }
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
                retire(*entry);
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
        block &insert(std::uint32_t pc_mode, const core::code_mapping &view, std::size_t size, unsigned policy = 0) {
            const auto k = key(view.address_space, pc_mode);
            forget_dispatch(k);
            auto old = current_.find(k);
            if (old != current_.end()) retire(versions_[old->second]);
            const auto version = static_cast<std::uint32_t>(versions_.size());
            versions_.push_back({k, version, view.bytes, {view.bytes, view.bytes + size}});
            current_[k] = version;
            auto &entry = versions_.back();
            entry.policy = policy;
            entry.guard_begin = reinterpret_cast<std::uintptr_t>(view.bytes);
            entry.guard_end = entry.guard_begin + size;
            index_span(entry.version, pc_mode & ~1u, size);
            return entry;
        }

        // Reuse code at the same guest PC across processes only when both the
        // executable bytes and every folded code dependency are identical.
        // Each process retains its own mapping/lifetime guards. Validation
        // happens at compilation, never on the hot path.
        aot_func share(std::uint32_t pc_mode, const core::code_mapping &view, core &cpu, unsigned policy = 0) {
            const auto found = shared_entries_.find(pc_mode);
            if (found == shared_entries_.end()) return nullptr;
            for (auto version : found->second) {
                auto &source = versions_[version];
                if (!source.live || !source.function || source.policy != policy || !view.bytes
                        || source.code.size() > view.size
                        || !equal_code_bytes(view.bytes, source.code.data(), source.code.size())) continue;
                bool valid = true;
                std::vector<const std::uint8_t *> dependencies;
                dependencies.reserve(source.dependencies.size());
                for (const auto &dependency : source.dependencies) {
                    core::code_mapping mapped;
                    if (!cpu.resolve_code || !cpu.resolve_code(dependency.address, mapped)
                            || mapped.address_space != view.address_space
                            || !mapped.bytes || mapped.size < dependency.code.size()
                            || !equal_code_bytes(mapped.bytes, dependency.code.data(), dependency.code.size())) { valid = false; break; }
                    dependencies.push_back(mapped.bytes);
                }
                if (!valid) continue;
                if (!source.function_lifetime) source.function_lifetime = std::make_shared<unsigned char>(0);
                auto &entry = insert(pc_mode, view, source.code.size(), policy);
                entry.function_lifetime = source.function_lifetime;
                for (std::size_t i = 0; i < source.dependencies.size(); ++i) {
                    const auto &dependency = source.dependencies[i];
                    add_dependency(entry, dependency.address, dependencies[i], dependency.code);
                }
                attach(entry.version, source.function);
                return entry.function;
            }
            return nullptr;
        }

        void add_dependency(block &entry, std::uint32_t address,
            const std::uint8_t *backing, const std::vector<std::uint8_t> &bytes) {
            entry.dependencies.push_back({address, backing, bytes});
            const auto begin = reinterpret_cast<std::uintptr_t>(backing);
            entry.guard_begin = std::min(entry.guard_begin, begin);
            entry.guard_end = std::max(entry.guard_end, begin + bytes.size());
            index_span(entry.version, address, bytes.size());
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

        bool attach(std::uint32_t version, aot_func function) {
            if (version < versions_.size() && versions_[version].live) {
                forget_dispatch(versions_[version].key);
                versions_[version].function = function;
                auto &shared = shared_entries_[static_cast<std::uint32_t>(versions_[version].key)];
                if (shared.empty() || shared.back() != version) shared.push_back(version);
                return true;
            }
            return false;
        }

        void retire(block &entry) {
            if (entry.unloaded) return;
            forget_dispatch(entry.key);
            auto &recent = recent_[recent_index(entry.key)];
            if (recent == &entry) recent = nullptr;
            const auto current = current_.find(entry.key);
            if (current != current_.end() && current->second == entry.version) current_.erase(current);
            entry.live = false;
            entry.unloaded = true;
            retired_versions_.push_back(entry.version);
        }

        bool was_unloaded(std::uint32_t version) const {
            return version < versions_.size() && versions_[version].unloaded;
        }

        bool has_retired() const { return !retired_versions_.empty(); }

        aot_func function_for_version(std::uint32_t version) const {
            return version < versions_.size() && versions_[version].live
                ? versions_[version].function : nullptr;
        }

        // Only call after generated code has returned. Version IDs remain
        // tombstones so a late staged module cannot attach to a replacement.
        std::vector<aot_func> collect_retired() {
            std::vector<aot_func> functions;
            std::vector<std::uint32_t> pages;
            std::vector<std::uint32_t> pcs;
            const auto span = [&](std::uint32_t address, std::size_t size) {
                if (!size) return;
                const auto end = std::uint64_t(address) + std::min<std::uint64_t>(size, 0x100000000ull - address);
                for (auto page = std::uint64_t(address) >> 12; page <= ((end - 1) >> 12); ++page)
                    pages.push_back(static_cast<std::uint32_t>(page));
            };
            for (auto version : retired_versions_) {
                auto &entry = versions_[version];
                if (entry.function && (!entry.function_lifetime || entry.function_lifetime.use_count() == 1))
                    functions.push_back(entry.function);
                entry.function_lifetime.reset();
                pcs.push_back(static_cast<std::uint32_t>(entry.key));
                span(static_cast<std::uint32_t>(entry.key) & ~1u, entry.code.size());
                for (const auto &dependency : entry.dependencies) span(dependency.address, dependency.code.size());
                std::vector<std::uint8_t>{}.swap(entry.code);
                std::vector<dependency>{}.swap(entry.dependencies);
                entry.function = nullptr;
                entry.backing = nullptr;
                entry.mapping_source = nullptr;
                entry.mapping_generation = 0;
                entry.guard_begin = entry.guard_end = 0;
            }
            retired_versions_.clear();
            std::sort(pcs.begin(), pcs.end());
            pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
            for (auto pc : pcs) {
                const auto found = shared_entries_.find(pc);
                if (found == shared_entries_.end()) continue;
                auto &versions = found->second;
                versions.erase(std::remove_if(versions.begin(), versions.end(),
                    [&](auto version) { return versions_[version].unloaded; }), versions.end());
                if (versions.empty()) shared_entries_.erase(found);
            }
            std::sort(pages.begin(), pages.end());
            pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
            for (auto page : pages) {
                const auto found = invalidation_pages_.find(page);
                if (found == invalidation_pages_.end()) continue;
                auto &versions = found->second;
                versions.erase(std::remove_if(versions.begin(), versions.end(),
                    [&](auto version) { return versions_[version].unloaded; }), versions.end());
                if (versions.empty()) invalidation_pages_.erase(found);
            }
            return functions;
        }

        void invalidate(std::uint32_t address, std::size_t size) {
            if (!size) return;
            const auto end = std::uint64_t(address) + std::min<std::uint64_t>(size, 0x100000000ull - address);
            // Data-page unmaps must not walk every translated region. The index
            // overapproximates by page; retain exact primary/dependency overlap
            // checks and invalidate every address space, as before.
            for (auto page = invalidation_pages_.lower_bound(address >> 12);
                    page != invalidation_pages_.end() && (std::uint64_t(page->first) << 12) < end; ++page) {
                for (auto version : page->second) {
                    auto &entry = versions_[version];
                    if (entry.unloaded) continue;
                    const auto start = static_cast<std::uint32_t>(entry.key) & ~1u;
                    const bool dependency_hit = std::any_of(entry.dependencies.begin(), entry.dependencies.end(),
                        [&](const auto &d) { return d.address < end && std::uint64_t(d.address) + d.code.size() > address; });
                    if (dependency_hit || (start < end && std::uint64_t(start) + entry.code.size() > address)) {
                        const bool was_live = entry.live;
                        retire(entry);
                        if (was_live) ++invalidations;
                    }
                }
            }
        }

        void retire_space(std::uint32_t space) {
            std::vector<std::uint32_t> versions;
            for (const auto &[k, version] : current_)
                if (k >> 32 == space) versions.push_back(version);
            for (auto version : versions) retire(versions_[version]);
        }

        std::size_t versions() const { return versions_.size(); }
        std::uint64_t invalidations = 0;

    private:
        void index_span(std::uint32_t version, std::uint32_t address, std::size_t size) {
            if (!size) return;
            const auto end = std::uint64_t(address) + std::min<std::uint64_t>(size, 0x100000000ull - address);
            for (auto page = std::uint64_t(address) >> 12; page <= ((end - 1) >> 12); ++page) {
                auto &versions = invalidation_pages_[static_cast<std::uint32_t>(page)];
                if (versions.empty() || versions.back() != version) versions.push_back(version);
            }
        }

        template <typename Snapshot>
        static bool snapshot_equal(const Snapshot &snapshot) {
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
        // Deque identities stay stable; dead payloads are freed at a CPU boundary.
        std::array<block *, 4096> recent_{};
        struct dispatch_entry {
            std::uint64_t key = 0, generation = 0;
            const std::atomic<std::uint64_t> *source = nullptr;
            aot_func function = nullptr;
        };
        std::vector<dispatch_entry> dispatch_ = std::vector<dispatch_entry>(4096);
        // Version IDs are never reused by a late module instantiation.
        std::deque<block> versions_;
        std::vector<std::uint32_t> retired_versions_;
        std::unordered_map<std::uint64_t, std::uint32_t> current_;
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> shared_entries_;
        // Reclamation removes dead versions from the touched page buckets.
        std::map<std::uint32_t, std::vector<std::uint32_t>> invalidation_pages_;
    };
}
