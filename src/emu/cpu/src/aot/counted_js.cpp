#include <cpu/aot/counted_js.h>
#if defined(EKA2L1_COUNTED_DIRECT_JS)
#include <cpu/aot/direct_js.h>
#endif
#ifndef EKA2L1_COUNTED_DIRECT_OBSERVE
#define EKA2L1_COUNTED_DIRECT_OBSERVE(event) ((void)0)
#endif
#include <cpu/aot/loop_pattern.h>
#if defined(EKA2L1_PACKED_PIXEL_JS)
#include <cpu/aot/packed_pixel_pattern.h>
#endif
#include <cpu/aot/region_pattern.h>
#if defined(EKA2L1_COUNTED_REGION_JS)
#include <cpu/aot/region_js.h>
#endif
#if defined(EKA2L1_COUNTED_LOOP_JS)
#include <cpu/aot/loop_js.h>
#include <cpu/aot/semantic_js.h>
#endif
#include <cpu/dyncom/armstate.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace eka2l1::arm::counted_js {
    struct entry {
        std::uint32_t asid, pc, function;
        bool shared, thumb;
        std::vector<std::uint32_t> words;
        std::uint32_t loop_function = ~std::uint32_t(0);
#if defined(EKA2L1_PACKED_PIXEL_JS)
        std::uint32_t packed_pixel = ~std::uint32_t(0);
        std::uint32_t palette_pixel = ~std::uint32_t(0);
        std::uint32_t blend_pixel = ~std::uint32_t(0);
#endif
        std::uint32_t region_index = ~std::uint32_t(0), region_member = 0;
#if defined(EKA2L1_COUNTED_WASM) && defined(EKA2L1_COUNTED_DIRECT_JS)
        bool direct_dispatch = false;
#endif
    };
    struct stamp {
        bool accepted = false;
        std::uint32_t generation = 0;
        std::uint64_t mapping = 0;
        std::size_t ptr = 0;
        std::uint32_t host = 0;
        std::vector<std::size_t> instructions;
    };
#if defined(EKA2L1_COUNTED_REGION_JS)
    struct region_template {
        region_js::invocation_context context{};
        std::uint64_t mapping = 0;
        std::uint32_t generation = 0;
        bool valid = false;
        bool active = false;
    };
#if defined(EKA2L1_COOPERATIVE_JS)
    static_assert(sizeof(region_template) <= 256);
#endif
#endif
    struct address_space_cache {
        std::uint32_t asid;
        std::vector<stamp> stamps;
#if defined(EKA2L1_COUNTED_REGION_JS)
        std::vector<region_template> region_templates;
        bool regions_unavailable = false;
#endif
    };
    struct cpu_cache {
        ARMul_State *cpu;
        std::vector<std::unique_ptr<address_space_cache>> spaces;
        address_space_cache *last_space = nullptr;
        std::uint64_t last_asid = ~std::uint64_t(0);
        bool pending_resume = false;
        std::size_t resume_ptr = 0;
    };
    static_assert(sizeof(entry) <= 64 && sizeof(stamp) <= 64);
#if defined(EKA2L1_COOPERATIVE_JS)
    static_assert(sizeof(std::size_t) == 4);
#if defined(EKA2L1_COUNTED_DIRECT_JS)
    static_assert(offsetof(stamp, accepted) == 0 && offsetof(stamp, generation) == 4
        && offsetof(stamp, mapping) == 8 && offsetof(stamp, ptr) == 16 && offsetof(stamp, host) == 20);
#endif
#endif
    bool enabled = false;
    static executor invoke = nullptr;
    static executor untracked = nullptr;
    static bool tracking = false;
    static std::vector<entry> entries;
#if defined(EKA2L1_PACKED_PIXEL_JS)
    static std::vector<loop_js::packed_pixel_pattern> packed_pixels;
    static std::vector<loop_js::palette_pixel_pattern> palette_pixels;
    static std::vector<loop_js::blend_pixel_pattern> blend_pixels;
#endif
#if defined(EKA2L1_COUNTED_REGION_JS)
    struct region_record {
        std::uint32_t function, count;
        std::array<std::size_t,region_js::member_limit> indices{};
    };
    static std::vector<region_record> regions;
#endif
    static std::unordered_map<std::uint64_t, std::size_t> indices;
    static std::unordered_map<std::uint32_t, std::size_t> shared_indices;
    struct lookup_slot {
        std::uint32_t asid = 0, pc = 0, index = 0;
        bool valid = false;
    };
    static_assert(sizeof(lookup_slot) <= 16);
    static constexpr std::size_t lookup_sets = 2048;
    static std::array<std::array<lookup_slot, 2>, lookup_sets> lookup_cache{};
    static std::array<std::uint8_t, lookup_sets> lookup_replace{};
    static std::vector<std::unique_ptr<cpu_cache>> caches;
    static cpu_cache *last = nullptr;
    static std::size_t stamp_spaces = 0;
    static ARMul_State *decoding = nullptr;
    static std::size_t decoding_entry = 0, decoded = 0;
    static std::vector<std::size_t> decoded_pointers;
    static bool matches = false, decoding_thumb = false;
    static std::uint64_t decoding_mapping = 0;
    static std::uint32_t decoding_generation = 0, decoding_asid = 0;
    static std::uint32_t first_pc = 0, last_pc = 0;
    static std::uint64_t call_count = 0, instruction_count = 0;
    static std::uint32_t tracked(ARMul_State *cpu, std::uint32_t function) {
        const auto count = untracked(cpu, function);
        if (count) { ++call_count; instruction_count += count; }
        return count;
    }
#if defined(EKA2L1_COUNTED_REGION_JS)
    static void require_idle_regions(const cpu_cache &owner) {
        for (const auto &space : owner.spaces) for (const auto &item : space->region_templates)
            if (item.active) throw std::runtime_error("Lifetime change during region invocation");
    }
#endif

    static std::uint64_t mapping(ARMul_State *cpu) {
        const auto *generation = cpu->parent()->code_mapping_generation;
        return generation ? generation->load(std::memory_order_acquire) : 0;
    }
    static cpu_cache &cache(ARMul_State *cpu) {
        if (last && last->cpu == cpu) return *last;
        for (auto &item : caches) if (item->cpu == cpu) { last = item.get(); return *last; }
        if (caches.size() == 16) throw std::runtime_error("Counted CPU cache bound exceeded");
        caches.push_back(std::make_unique<cpu_cache>(cpu_cache{cpu}));
        last = caches.back().get();
        return *last;
    }
    // Candidate words can be shared; decoded visibility and admission cannot.
    // Retain each actual ASID's independently authenticated original entry.
    static address_space_cache *stamps(ARMul_State *cpu) {
        auto &owner = cache(cpu);
        const auto asid = cpu->instruction_cache_asid;
        if (owner.last_asid == asid && (owner.last_space || stamp_spaces == 16)) return owner.last_space;
        owner.last_asid = asid;
        for (auto &space : owner.spaces) if (space->asid == asid) {
            owner.last_space = space.get(); return owner.last_space;
        }
        // Bound total stamp vectors by the original sixteen-CPU allocation
        // budget. New ASIDs fall back without evicting authenticated entries.
        if (stamp_spaces == 16) { owner.last_space = nullptr; return nullptr; }
        owner.spaces.push_back(std::make_unique<address_space_cache>(
            address_space_cache{asid, std::vector<stamp>(entries.size())}));
        ++stamp_spaces;
        owner.last_space = owner.spaces.back().get();
        return owner.last_space;
    }
    static std::size_t find(ARMul_State *cpu, std::uint32_t address) {
        EKA2L1_COUNTED_DIRECT_OBSERVE(1);
        if (address < first_pc || address > last_pc) return entries.size();
        const auto asid = cpu->instruction_cache_asid;
        const auto pc = address | (cpu->TFlag ? 1u : 0u);
        const auto set = ((pc >> 1) ^ (pc >> 12) ^ asid) & (lookup_sets - 1);
        for (const auto &slot : lookup_cache[set])
            if (slot.valid && slot.asid == asid && slot.pc == pc) return slot.index;
        const auto key = cpu->make_instruction_cache_key(address);
        const auto found = indices.find(key);
        std::size_t index;
        if (found != indices.end()) index = found->second;
        else {
            const auto shared = shared_indices.find(pc);
            index = shared == shared_indices.end() ? entries.size() : shared->second;
        }
        // Memoize immutable registry candidates, including misses. Every actual
        // decoded-entry and mapping/L1 admission check still runs in execute.
        auto &next = lookup_replace[set];
        lookup_cache[set][next] = {asid, pc, static_cast<std::uint32_t>(index), true};
        next ^= 1;
        return index;
    }
    static std::size_t find(ARMul_State *cpu) {
        return find(cpu, cpu->Reg[15]);
    }
    void reset() {
#if defined(EKA2L1_COUNTED_LOOP_JS)
        semantic_js::reset();
#endif
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::reset();
#endif
#if defined(EKA2L1_COUNTED_REGION_JS)
        for (const auto &owner : caches) require_idle_regions(*owner);
#endif
        enabled = false; invoke = untracked = nullptr; tracking = false; decoding = nullptr; last = nullptr;
        caches.clear(); stamp_spaces = 0; entries.clear(); first_pc = last_pc = 0;
#if defined(EKA2L1_PACKED_PIXEL_JS)
        packed_pixels.clear(); palette_pixels.clear(); blend_pixels.clear();
#endif
        indices.clear(); shared_indices.clear();
#if defined(EKA2L1_COUNTED_REGION_JS)
        regions.clear();
#endif
        for (auto &set : lookup_cache) for (auto &slot : set) slot.valid = false;
        lookup_replace.fill(0);
        call_count = instruction_count = 0;
    }
    bool add(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
             std::size_t count, std::uint32_t function, bool shared, bool thumb) {
        if (enabled || !words || !count || count > 128 || entries.size() == entry_limit
            || (pc & (thumb ? 1u : 3u)) || count * (thumb ? 2u : 4u) > 4096 - (pc & 4095)) return false;
        if (thumb && std::any_of(words, words + count, [](auto word) { return word > 0xffff; })) return false;
        const auto tagged_pc = pc | (thumb ? 1u : 0u);
        for (const auto &item : entries) if (item.asid == asid && item.pc == pc && item.thumb == thumb) return false;
        if (shared && shared_indices.find(tagged_pc) != shared_indices.end()) return false;
        indices.emplace((std::uint64_t(asid) << 32) | tagged_pc, entries.size());
        if (shared) shared_indices.emplace(tagged_pc, entries.size());
        entries.push_back({asid, pc, function, shared, thumb, std::vector<std::uint32_t>(words, words + count)});
#if defined(EKA2L1_COUNTED_WASM) && defined(EKA2L1_COUNTED_DIRECT_JS)
        const auto word = words[count - 1];
        entries.back().direct_dispatch = thumb
            ? ((word & 0xff00) == 0x4700 || (word & 0xf800) == 0xe800
                || (word & 0xff00) == 0xbd00 || (word & 0xff00) == 0xdf00
                || ((word & 0xfc00) == 0x4400 && ((word & 7) | ((word >> 4) & 8)) == 15))
            : ((word >> 28) == 15 || (word & 0x0ffffff0) == 0x012fff10
                || (word & 0x0ffffff0) == 0x012fff30 || ((word >> 25) & 7) == 7
                || (((word >> 25) & 7) == 4 && (word & 0x8000))
                || (((word >> 25) & 7) <= 3 && (word & 0xf000) == 0xf000));
#endif
        first_pc = entries.size() == 1 ? pc : std::min(first_pc, pc);
        last_pc = std::max(last_pc, pc);
        return true;
    }
    void install(executor execute, bool diagnostic) {
        if (enabled || !execute || entries.empty()) throw std::runtime_error("Invalid counted installation");
        untracked = execute; tracking = diagnostic;
        invoke = diagnostic ? tracked : execute; enabled = true;
    }
    void begin_decode(ARMul_State *cpu) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        decoding = nullptr;
        if (!enabled || cpu->NumInstrsToExecute == 1) return;
        const auto index = find(cpu);
        if (index == entries.size()) return;
        decoding = cpu; decoding_entry = index; decoded = 0; matches = true; decoded_pointers.clear();
        decoding_mapping = mapping(cpu); decoding_generation = cpu->trans_gen;
        decoding_asid = cpu->instruction_cache_asid; decoding_thumb = cpu->TFlag;
    }
    void decoded_word(ARMul_State *cpu, std::uint32_t pc, std::uint32_t word, std::size_t ptr) {
        if (decoding != cpu) return;
        const auto &item = entries[decoding_entry];
        matches = matches && decoded < item.words.size()
            && pc == item.pc + decoded * (item.thumb ? 2u : 4u) && word == item.words[decoded];
        if (decoded < item.words.size()) decoded_pointers.push_back(ptr);
        ++decoded;
    }
    void finish_decode(ARMul_State *cpu, std::size_t ptr) {
        if (decoding != cpu) return;
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::invalidate();
#endif
        auto *space = stamps(cpu);
        if (!space) { decoding = nullptr; return; }
        auto &proof = space->stamps[decoding_entry];
        const auto &item = entries[decoding_entry];
#if defined(EKA2L1_COUNTED_REGION_JS)
        if (item.region_index < space->region_templates.size())
            space->region_templates[item.region_index].valid = false;
#endif
        const auto bytes = item.words.size() * (item.thumb ? 2u : 4u);
        core::code_mapping view;
        const bool mapped = cpu->parent()->resolve_code && cpu->parent()->resolve_code(item.pc, view)
            && view.bytes && view.size >= bytes && view.address_space == decoding_asid
            && reinterpret_cast<std::uintptr_t>(view.bytes) <= 0xffffffffu - bytes;
        proof = {matches && decoded == item.words.size() && bool(cpu->TFlag) == item.thumb
            && decoding_thumb == item.thumb
            && cpu->instruction_cache_asid == decoding_asid && (item.shared || decoding_asid == item.asid)
            && cpu->NumInstrsToExecute
            && decoding_generation == cpu->trans_gen && decoding_mapping == mapping(cpu) && mapped,
            cpu->trans_gen, decoding_mapping, ptr,
            mapped ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(view.bytes)) : 0, decoded_pointers};
        decoding = nullptr;
    }
    // Both executors use exactly the same original decoded visibility proof.
    template<bool check_budget = true, bool check_l1 = true>
    static bool admit(ARMul_State *cpu, std::uint32_t prefix, std::size_t index, stamp *&accepted
#if defined(EKA2L1_COUNTED_DIRECT_JS)
            , address_space_cache **direct_space = nullptr
#endif
            ) {
        EKA2L1_COUNTED_DIRECT_OBSERVE(2);
        const auto &item = entries[index];
        const auto stride = item.thumb ? 2u : 4u;
        if constexpr (check_budget)
            if (cpu->NumInstrsToExecute < prefix || cpu->NumInstrsToExecute - prefix < item.words.size()) return false;
        auto &owner = cache(cpu);
        owner.pending_resume = false;
        auto *space = stamps(cpu);
        if (!space) return false;
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        if (direct_space) *direct_space = space;
#endif
        auto &proof = space->stamps[index];
        accepted = &proof;
        if (!proof.accepted || proof.generation != cpu->trans_gen) return false;
        const auto current_mapping = mapping(cpu);
        if (proof.mapping != current_mapping) {
            core::code_mapping view;
            const bool same = cpu->parent()->resolve_code && cpu->parent()->resolve_code(item.pc, view)
                && view.address_space == cpu->instruction_cache_asid && view.size >= item.words.size() * stride
                && reinterpret_cast<std::uintptr_t>(view.bytes) == proof.host;
            if (!same) { proof.accepted = false; return false; }
            // Unrelated mappings do not invalidate this existing decoded view.
            // Revalidate only its physical identity, once per mapping generation.
            proof.mapping = current_mapping;
        }
        // The stamp was installed only with this original decoded entry. Cache
        // clears bump trans_gen; raw writes without IMB retain the same decoded
        // instruction visibility in both routes, without re-reading new bytes.
        if constexpr (check_l1) {
            const auto key = cpu->make_instruction_cache_key(item.pc);
            auto &slot = cpu->block_l1_cache[ARMul_State::block_l1_index(key)];
            if (slot.key != key) {
#if defined(EKA2L1_COUNTED_LOOP_JS)
                if (!loop_js::enabled || item.loop_function == ~std::uint32_t(0)
                    || ((item.loop_function & semantic_js::function_bit)
                        && !semantic_js::original_fallback(item.loop_function & ~semantic_js::function_bit))) return false;
                // Original branch links can enter this decoded block without
                // refilling a colliding L1 slot. Recover only the identical
                // authenticated entry from the full ASID/PC/T cache key.
                const auto found = cpu->instruction_cache.find(key);
                if (found == cpu->instruction_cache.end() || found->second != proof.ptr) return false;
                slot = {key, found->second};
#else
                return false;
#endif
            }
            if (slot.ptr != proof.ptr) return false;
        }
        return true;
    }
#if defined(EKA2L1_COUNTED_REGION_JS)
    bool bind_region(const region_js::member *members, std::size_t count, std::uint32_t function) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        if (!enabled || !members || count < 2 || count > region_js::member_limit
                || function >= entry_limit || regions.size() >= entry_limit) return false;
        region_record record{function,static_cast<std::uint32_t>(count)};
        for (std::size_t n = 0; n < count; ++n) {
            const auto &member = members[n];
            if (member.asid != members[0].asid || !region_js::block_words(member.words,member.count,member.pc)) return false;
            const auto key = (std::uint64_t(member.asid) << 32) | member.pc;
            const auto found = indices.find(key);
            if (found == indices.end()) return false;
            const auto &item = entries[found->second];
            if (item.thumb || item.words.size() != member.count
                    || !std::equal(item.words.begin(),item.words.end(),member.words)
                    || item.loop_function != ~std::uint32_t(0) || item.region_index != ~std::uint32_t(0)) return false;
            for (std::size_t j = 0; j < n; ++j) if (record.indices[j] == found->second) return false;
            record.indices[n] = found->second;
        }
        for (std::size_t n = 0; n < count; ++n) {
            entries[record.indices[n]].region_index = static_cast<std::uint32_t>(regions.size());
            entries[record.indices[n]].region_member = static_cast<std::uint32_t>(n);
        }
        regions.push_back(record);
        return true;
    }
    void clear_regions() {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        for (const auto &owner : caches) require_idle_regions(*owner);
        for (auto &item : entries) item.region_index = ~std::uint32_t(0);
        regions.clear();
        for (auto &owner : caches) for (auto &space : owner->spaces) {
            space->region_templates.clear(); space->regions_unavailable = false;
        }
    }
    static void publish_region_state(ARMul_State *cpu) {
        cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
            | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
    }
    static std::uint32_t invoke_region(ARMul_State *cpu, std::uint32_t prefix, std::size_t index,
            const stamp &start_proof) {
        const auto &entry = entries[index];
        const auto &record = regions.at(entry.region_index);
        auto *generation = cpu->parent()->code_mapping_generation;
        if (!generation) return 0;
        const auto address = [](const auto *value) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
        };
        // The caller has already admitted this start through this actual space.
        auto &space = *cache(cpu).last_space;
        if (space.regions_unavailable) return 0;
        if (space.region_templates.empty()) {
            try { space.region_templates.resize(regions.size()); }
            catch (const std::bad_alloc &) { space.regions_unavailable = true; return 0; }
        }
        auto &prepared = space.region_templates.at(entry.region_index);
        if (prepared.active) throw std::runtime_error("Nested cached region invocation");
        auto &context = prepared.context;
        if (!prepared.valid || prepared.generation != start_proof.generation
                || prepared.mapping != start_proof.mapping) {
            context = {};
            context.generation = address(&cpu->trans_gen); context.mapping = address(generation);
            context.asid = address(&cpu->instruction_cache_asid); context.tlb = address(&cpu->mem_cache_);
            for (unsigned n = 0; n < record.count; ++n) {
                const auto member = record.indices[n];
                const auto &item = entries[member];
                const auto key = cpu->make_instruction_cache_key(item.pc);
                const auto &slot = cpu->block_l1_cache[ARMul_State::block_l1_index(key)];
                auto &target = context.members[n];
                target.l1_key = address(&slot.key); target.l1_pointer = address(&slot.ptr);
                stamp *accepted = nullptr;
                if (member != index && !admit<false,false>(cpu,0,member,accepted)) continue;
                const auto *proof = member == index ? &start_proof : accepted;
                context.ready |= 1u << n;
                target.decoded_pointer = static_cast<std::uint32_t>(proof->ptr);
                target.host_begin = proof->host;
                target.host_end = proof->host + static_cast<std::uint32_t>(item.words.size()*4);
            }
            prepared.generation = start_proof.generation; prepared.mapping = start_proof.mapping;
            prepared.valid = true;
        }
        context.budget = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            cpu->NumInstrsToExecute-prefix,region_js::progress_limit));
        context.start = entry.region_member;
        if (!(context.ready & (1u << context.start))) return 0;
        struct active_scope {
            bool &active;
            explicit active_scope(bool &value) : active(value) { active = true; }
            ~active_scope() { active = false; }
        } active(prepared.active);
        auto *tlb = cpu->mem_cache_;
        cpu->aot_tlb = tlb && tlb->page_bits == 12 ? address(tlb->entries) : 0;
        cpu->aot_exit = 0; publish_region_state(cpu);
        const auto progress = region_js::invoke_admitted(cpu,record.function,context);
        const auto count = progress & region_js::progress_limit;
        const auto member = (progress >> region_js::member_shift) & 7u;
        const auto part = (progress >> region_js::prefix_shift) & 255u;
        const bool complete = progress & region_js::complete_bit;
        if ((progress >> 28) || !count || count > context.budget || member >= record.count
                || !(context.ready & (1u << member))) throw std::runtime_error("Invalid region cumulative progress");
        const auto &last_item = entries[record.indices[member]];
        if (!part || part > last_item.words.size() || count < part
                || (complete ? part != last_item.words.size()
                    : part == last_item.words.size() || cpu->TFlag || cpu->Reg[15] != last_item.pc+part*4))
            throw std::runtime_error("Invalid region original prefix");
        if (complete) {
            const auto terminal = last_item.words.back();
            if ((terminal & 0x0e000000u) == 0x0a000000u) {
                const auto displacement = static_cast<std::int32_t>(terminal << 8) >> 6;
                const auto fallthrough = last_item.pc + static_cast<std::uint32_t>(last_item.words.size()*4);
                const auto target = fallthrough + 4u + static_cast<std::uint32_t>(displacement);
                if (cpu->TFlag || (cpu->Reg[15] != target && cpu->Reg[15] != fallthrough))
                    throw std::runtime_error("Invalid region direct boundary");
            } else if (cpu->TFlag > 1u || (cpu->Reg[15] & 1u) != cpu->TFlag)
                throw std::runtime_error("Invalid region interworking boundary");
        }
        if (!complete && cpu->NumInstrsToExecute) {
            auto &owner = cache(cpu);
            owner.pending_resume = true;
            owner.resume_ptr = space.stamps[record.indices[member]].instructions.at(part);
        }
        publish_region_state(cpu); region_js::record(count);
        return count | (complete ? region_js::complete_bit : 0u);
    }
#endif
    bool bind_loop(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
            std::size_t count, std::uint32_t function) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        if (!enabled || function >= entry_limit
            || !loop_js::pure_loop_words(words, count, pc)) return false;
        for (auto &item : entries) if (item.asid == asid && item.pc == pc && !item.thumb
                && item.words.size() == count && std::equal(item.words.begin(), item.words.end(), words)) {
            if (item.loop_function != ~std::uint32_t(0) || item.region_index != ~std::uint32_t(0)) return false;
            item.loop_function = function;
            return true;
        }
        return false;
    }
    bool bind_word_loop(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
            std::size_t count, std::uint32_t function) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        if (!enabled || function >= entry_limit
            || !loop_js::word_loop_words(words, count, pc)) return false;
        for (auto &item : entries) if (item.asid == asid && item.pc == pc && !item.thumb
                && item.words.size() == count && std::equal(item.words.begin(), item.words.end(), words)) {
            if (item.loop_function != ~std::uint32_t(0) || item.region_index != ~std::uint32_t(0)) return false;
            // The high bit distinguishes the explicitly versioned progress ABI.
            item.loop_function = function | 0x80000000u;
#if defined(EKA2L1_PACKED_PIXEL_JS)
            loop_js::packed_pixel_pattern pattern;
            if (loop_js::recognize_packed_pixel(words, count, pattern)) {
                pattern.table = loop_js::prepare_packed_pixel(pattern);
                if (pattern.table != 255) {
                    item.packed_pixel = packed_pixels.size(); packed_pixels.push_back(pattern);
                }
            }
            loop_js::palette_pixel_pattern palette;
            if (loop_js::recognize_palette_pixel(words, count, palette)) {
                item.palette_pixel = palette_pixels.size(); palette_pixels.push_back(palette);
            }
            loop_js::blend_pixel_pattern blend;
            if (loop_js::recognize_blend_pixel(words, count, blend)) {
                item.blend_pixel = blend_pixels.size(); blend_pixels.push_back(blend);
            }
#endif
            return true;
        }
        return false;
    }
    bool bind_semantic(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
            std::size_t count, std::uint32_t function) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        if (!enabled || !words || !count || function >= 0x40000000u) return false;
        for (auto &item : entries) if (item.asid == asid && item.pc == pc && !item.thumb
                && item.words.size() == count && std::equal(item.words.begin(), item.words.end(), words)) {
            if (item.loop_function != ~std::uint32_t(0) || item.region_index != ~std::uint32_t(0)) return false;
            item.loop_function = function | 0x40000000u;
            return true;
        }
        return false;
    }
    void clear_semantics() {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        for (auto &item : entries) if (item.loop_function != ~std::uint32_t(0)
                && (item.loop_function & 0x40000000u)) item.loop_function = ~std::uint32_t(0);
    }
    void clear_loops() {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        for (auto &item : entries) {
            item.loop_function = ~std::uint32_t(0);
#if defined(EKA2L1_PACKED_PIXEL_JS)
            item.packed_pixel = ~std::uint32_t(0);
            item.palette_pixel = ~std::uint32_t(0);
            item.blend_pixel = ~std::uint32_t(0);
#endif
        }
#if defined(EKA2L1_PACKED_PIXEL_JS)
        packed_pixels.clear(); palette_pixels.clear(); blend_pixels.clear();
#endif
    }
    bool admit_loop(ARMul_State *cpu, std::uint32_t prefix, loop_admission &result) {
        if (!enabled) return false;
        const auto index = find(cpu);
        if (index == entries.size() || entries[index].loop_function == ~std::uint32_t(0)) return false;
        stamp *proof = nullptr;
        if (!admit(cpu, prefix, index, proof)) return false;
        const auto &item = entries[index];
        result = {item.pc, static_cast<std::uint32_t>(item.words.size()),
            item.loop_function & 0x7fffffffu, proof->host,
            bool(item.loop_function & 0x80000000u), proof->instructions.data()};
#if defined(EKA2L1_PACKED_PIXEL_JS)
        if (item.packed_pixel < packed_pixels.size()) result.packed_pixel = &packed_pixels[item.packed_pixel];
        if (item.palette_pixel < palette_pixels.size()) result.palette_pixel = &palette_pixels[item.palette_pixel];
        if (item.blend_pixel < blend_pixels.size()) result.blend_pixel = &blend_pixels[item.blend_pixel];
#endif
        result.semantic = (result.function & 0x40000000u) != 0;
        return true;
    }
    void resume_loop(ARMul_State *cpu, const loop_admission &proof, std::uint32_t remainder) {
        if (!remainder || remainder >= proof.words || !proof.instructions)
            throw std::runtime_error("Invalid original loop resume prefix");
        auto &owner = cache(cpu);
        owner.pending_resume = true; owner.resume_ptr = proof.instructions[remainder];
    }
    static std::uint32_t invoke_generic(ARMul_State *cpu, std::uint32_t prefix,
            std::size_t index, stamp &proof) {
        const auto &item = entries[index];
        const auto stride = item.thumb ? 2u : 4u;
        const auto remaining = cpu->NumInstrsToExecute - prefix;
        auto *tlb = cpu->mem_cache_;
        cpu->aot_tlb = tlb && tlb->page_bits == 12
            ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(tlb->entries)) : 0;
        cpu->aot_exit = 0;
        cpu->aot_code_begin = proof.host; cpu->aot_code_end = proof.host + item.words.size() * stride;
        const auto count = invoke(cpu, item.function);
        if (!count)
            throw std::runtime_error("Counted executor returned zero after admission");
        if (count > item.words.size() || count > remaining)
            throw std::runtime_error("Counted executor exceeded admitted block");
        if (count && count < item.words.size() && cpu->NumInstrsToExecute) {
            if (cpu->Reg[15] != item.pc + count * stride || proof.instructions.size() != item.words.size())
                throw std::runtime_error("Unexpected counted partial boundary");
            auto &owner = cache(cpu);
            owner.pending_resume = true; owner.resume_ptr = proof.instructions[count];
        }
        cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
            | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
        return count | (count == item.words.size() ? 0x100u : 0u);
    }
#if defined(EKA2L1_COUNTED_DIRECT_JS)
#if defined(EKA2L1_COUNTED_WASM)
    std::uint32_t execute_native_direct(ARMul_State *cpu, direct_js::invocation_context *context) {
        const auto lifetime = *direct_js::epoch_address();
        const auto generation = cpu->trans_gen, asid = cpu->instruction_cache_asid;
        const auto *mapping_generation = reinterpret_cast<const std::atomic<std::uint64_t> *>(
            static_cast<std::uintptr_t>(context->mapping));
        const auto read_mapping = [mapping_generation]() {
            return mapping_generation ? mapping_generation->load(std::memory_order_acquire) : 0;
        };
        const auto current_mapping = read_mapping(), quantum = cpu->NumInstrsToExecute;
        const auto *owner = cpu->mem_cache_;
        const auto mode = cpu->TFlag, controls = cpu->Cpsr & 0x3df, tlb = cpu->aot_tlb;
        if (context->epoch != lifetime || context->start_index >= entries.size()
            || !context->budget || context->budget > direct_js::progress_limit
            || mode > 1 || quantum > 0xffffffffu || context->prefix > quantum
            || context->budget > quantum - context->prefix)
            throw std::runtime_error("Invalid native direct context");
        auto *proofs = reinterpret_cast<stamp *>(static_cast<std::uintptr_t>(context->stamps));
        std::size_t index = context->start_index;
        std::uint32_t total = 0, blocks = 0, last_index = context->start_index, last_prefix = 0;
        bool complete = false;
        while (blocks < direct_js::block_limit) {
            const auto &item = entries[index];
            const auto stride = item.thumb ? 2u : 4u;
            if (item.thumb != bool(mode) || item.words.size() > context->budget - total
                || (!item.thumb && item.words.size() == 5 && item.words.back() == 0x5afffffau)) break;
            const auto &proof = proofs[index];
            if (blocks) {
                if (item.loop_function != ~std::uint32_t(0) || item.region_index != ~std::uint32_t(0)) break;
                const auto key = cpu->make_instruction_cache_key(item.pc);
                const auto &slot = cpu->block_l1_cache[ARMul_State::block_l1_index(key)];
                if (!proof.accepted || proof.generation != generation || proof.mapping != current_mapping
                    || slot.key != key || slot.ptr != proof.ptr) break;
                cpu->Reg[15] = item.pc;
            }
            cpu->aot_tlb = tlb;
            cpu->aot_code_begin = proof.host; cpu->aot_code_end = proof.host + item.words.size() * stride;
            cpu->aot_exit = 0;
            const auto count = untracked(cpu, item.function);
            if (*direct_js::epoch_address() != lifetime)
                throw std::runtime_error("Native direct lifetime changed during function");
            if (!count || count > item.words.size() || count > context->budget - total)
                throw std::runtime_error("Invalid native direct function result");
            if (count < item.words.size()
                && (cpu->Reg[15] != item.pc + count * stride || cpu->TFlag != (item.thumb ? 1u : 0u)))
                throw std::runtime_error("Invalid native direct partial PC");
            total += count; ++blocks; complete = count == item.words.size();
            last_index = index; last_prefix = count;
            if (!complete || item.direct_dispatch || total == context->budget || blocks == direct_js::block_limit
                || cpu->aot_exit || cpu->trans_gen != generation || cpu->instruction_cache_asid != asid
                || cpu->mem_cache_ != owner || cpu->NumInstrsToExecute != quantum || cpu->TFlag != mode
                || (cpu->Cpsr & 0x3df) != controls || (!cpu->NirqSig && !(cpu->Cpsr & 128))
                || read_mapping() != current_mapping) break;
            index = find(cpu, cpu->Reg[15] & (mode ? 0xfffffffeu : 0xfffffffcu));
            if (index == entries.size()) break;
        }
        if (total) {
            context->last_index = last_index; context->last_prefix = last_prefix; context->blocks = blocks;
        }
        return total | (complete ? direct_js::complete_bit : 0u);
    }
#endif
    direct_js::configuration_layout direct_configuration() {
        const auto address = [](const auto *value) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
        };
        return {address(entries.data()), static_cast<std::uint32_t>(entries.size()), sizeof(entry),
            offsetof(entry, loop_function), offsetof(entry, region_index), sizeof(stamp),
            sizeof(ARMul_State), offsetof(ARMul_State, Reg) + 15 * sizeof(std::uint32_t),
            offsetof(ARMul_State, trans_gen), offsetof(ARMul_State, instruction_cache_asid),
            offsetof(ARMul_State, mem_cache_), offsetof(ARMul_State, block_l1_cache),
            sizeof(ARMul_State::block_l1_entry), offsetof(ARMul_State::block_l1_entry, ptr),
            offsetof(ARMul_State, NumInstrsToExecute), offsetof(ARMul_State, NirqSig),
            offsetof(ARMul_State, Cpsr), offsetof(ARMul_State, TFlag), offsetof(ARMul_State, aot_tlb),
            offsetof(ARMul_State, aot_code_begin), offsetof(ARMul_State, aot_code_end),
            offsetof(ARMul_State, aot_exit), address(direct_js::epoch_address())};
    }
    static std::uint32_t invoke_direct(ARMul_State *cpu, std::uint32_t prefix,
            std::size_t index, address_space_cache &space) {
        const auto &item = entries[index];
        if (!direct_js::enabled || item.loop_function != ~std::uint32_t(0)
            || item.region_index != ~std::uint32_t(0)
            || (!item.thumb && item.words.size() == 5 && item.words.back() == 0x5afffffau)
            || cpu->NumInstrsToExecute > 0xffffffffu || cpu->NumInstrsToExecute < prefix
            || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return 0;
        const auto address = [](const auto *value) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
        };
        auto *tlb = cpu->mem_cache_;
        cpu->aot_tlb = tlb && tlb->page_bits == 12 ? address(tlb->entries) : 0;
        auto *epoch = direct_js::epoch_address();
        direct_js::invocation_context context{
            address(space.stamps.data()), address(cpu->parent()->code_mapping_generation),
            static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(std::min<std::uint64_t>(
                cpu->NumInstrsToExecute - prefix, direct_js::progress_limit)), prefix, *epoch,
            static_cast<std::uint32_t>(index), 0, 0};
        const auto budget = context.budget, lifetime = *epoch;
        const auto result = direct_js::invoke(cpu, &context);
        // Zero is permitted only before any function ran. Effects never retry.
        if (!result) {
            if (context.blocks || context.last_prefix) throw std::runtime_error("Zero after direct effects");
            return 0;
        }
        const auto count = result & direct_js::progress_limit;
        const bool complete = result & direct_js::complete_bit;
        if (result & ~(direct_js::progress_limit | direct_js::complete_bit)
            || !count || count > budget || !context.blocks
            || context.blocks > direct_js::block_limit || context.blocks > count
            || context.last_index >= entries.size() || *epoch != lifetime || context.epoch != lifetime)
            throw std::runtime_error("Invalid direct execution progress");
        const auto &final = entries[context.last_index];
        if (!context.last_prefix || context.last_prefix > final.words.size()
            || context.last_prefix > count || complete != (context.last_prefix == final.words.size()))
            throw std::runtime_error("Invalid direct member progress");
        if (!complete) {
            if (cpu->Reg[15] != final.pc + context.last_prefix * (final.thumb ? 2u : 4u)
                || bool(cpu->TFlag) != final.thumb)
                throw std::runtime_error("Invalid direct original tail");
            if (cpu->NumInstrsToExecute) {
                const auto &proof = space.stamps[context.last_index];
                if (proof.instructions.size() != final.words.size())
                    throw std::runtime_error("Missing direct original tail");
                auto &owner = cache(cpu);
                owner.pending_resume = true; owner.resume_ptr = proof.instructions[context.last_prefix];
            }
        }
        cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
            | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
        if (tracking) { call_count += context.blocks; instruction_count += count; }
        direct_js::record(count, context.blocks);
        return result;
    }
#endif
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix) {
        if (!enabled) return 0;
        const auto index = find(cpu);
        if (index == entries.size()) return 0;
        stamp *proof = nullptr;
        if (!admit(cpu, prefix, index, proof)) return 0;
        return invoke_generic(cpu, prefix, index, *proof);
    }
#if defined(EKA2L1_COUNTED_LOOP_JS)
    std::uint32_t execute_dispatch(ARMul_State *cpu, std::uint32_t prefix) {
        if (!enabled) return 0;
        const auto index = find(cpu);
        if (index == entries.size()) return 0;
        stamp *proof = nullptr;
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        address_space_cache *direct_space = nullptr;
        if (!admit(cpu, prefix, index, proof, &direct_space)) return 0;
#else
        if (!admit(cpu, prefix, index, proof)) return 0;
#endif
        const auto &item = entries[index];
        if (loop_js::enabled && item.loop_function != ~std::uint32_t(0)) {
            loop_admission admitted{item.pc, static_cast<std::uint32_t>(item.words.size()),
                item.loop_function & 0x7fffffffu, proof->host,
                bool(item.loop_function & 0x80000000u), proof->instructions.data()};
#if defined(EKA2L1_PACKED_PIXEL_JS)
            if (item.packed_pixel < packed_pixels.size()) admitted.packed_pixel = &packed_pixels[item.packed_pixel];
            if (item.palette_pixel < palette_pixels.size()) admitted.palette_pixel = &palette_pixels[item.palette_pixel];
            if (item.blend_pixel < blend_pixels.size()) admitted.blend_pixel = &blend_pixels[item.blend_pixel];
#endif
            admitted.semantic = (admitted.function & semantic_js::function_bit) != 0;
            const auto progress = loop_js::execute_admitted(cpu, prefix, admitted);
            if (progress || !admitted.semantic || semantic_js::original_fallback(admitted.function & ~semantic_js::function_bit)) return progress;

        }
#if defined(EKA2L1_COUNTED_REGION_JS)
        if (region_js::enabled && item.region_index != ~std::uint32_t(0)) {
            const auto progress = invoke_region(cpu,prefix,index,*proof);
            if (progress) return progress;
        }
#endif
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        if (direct_js::enabled) {
            const auto progress = invoke_direct(cpu, prefix, index, *direct_space);
            if (progress) return progress;
        }
#endif
        const auto progress = invoke_generic(cpu, prefix, index, *proof);
        return (progress & 0xffu) | ((progress & 0x100u) ? loop_js::complete_bit : 0u);
    }
#endif
    bool resume(ARMul_State *cpu, std::size_t &ptr) {
        auto &owner = cache(cpu);
        if (!owner.pending_resume) return false;
        owner.pending_resume = false; ptr = owner.resume_ptr; return true;
    }
    void forget(ARMul_State *cpu) {
#if defined(EKA2L1_COUNTED_LOOP_JS)
        semantic_js::forget(cpu);
#endif
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::invalidate();
#endif
#if defined(EKA2L1_COUNTED_REGION_JS)
        for (const auto &owner : caches) if (owner->cpu == cpu) require_idle_regions(*owner);
#endif
        if (decoding == cpu) decoding = nullptr;
        if (last && last->cpu == cpu) last = nullptr;
        for (const auto &owner : caches) if (owner->cpu == cpu) stamp_spaces -= owner->spaces.size();
        caches.erase(std::remove_if(caches.begin(), caches.end(),
            [cpu](const auto &item) { return item->cpu == cpu; }), caches.end());
    }
    std::uint64_t calls() { return call_count; }
    std::uint64_t instructions() { return instruction_count; }
    bool diagnostics() { return tracking; }
}
