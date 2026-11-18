#include <cpu/aot/counted_js.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace eka2l1::arm::counted_js {
    struct entry {
        std::uint32_t asid, pc, function;
        std::vector<std::uint32_t> words;
    };
    struct stamp {
        bool accepted = false;
        std::uint32_t generation = 0;
        std::uint64_t mapping = 0;
        std::size_t ptr = 0;
        std::uint32_t host = 0;
        std::vector<std::size_t> instructions;
    };
    struct cpu_cache {
        ARMul_State *cpu;
        std::vector<stamp> stamps;
        bool pending_resume = false;
        std::size_t resume_ptr = 0;
    };
    bool enabled = false;
    static executor invoke = nullptr;
    static executor untracked = nullptr;
    static bool tracking = false;
    static std::vector<entry> entries;
    static std::unordered_map<std::uint64_t, std::size_t> indices;
    static std::uint64_t last_key = ~std::uint64_t(0);
    static std::size_t last_index = 0;
    static std::vector<std::unique_ptr<cpu_cache>> caches;
    static cpu_cache *last = nullptr;
    static ARMul_State *decoding = nullptr;
    static std::size_t decoding_entry = 0, decoded = 0;
    static std::vector<std::size_t> decoded_pointers;
    static bool matches = false;
    static std::uint64_t decoding_mapping = 0;
    static std::uint32_t decoding_generation = 0;
    static std::uint32_t first_pc = 0, last_pc = 0;
    static std::uint64_t call_count = 0, instruction_count = 0;
    static std::uint32_t tracked(ARMul_State *cpu, std::uint32_t function) {
        const auto count = untracked(cpu, function);
        if (count) { ++call_count; instruction_count += count; }
        return count;
    }

    static std::uint64_t mapping(ARMul_State *cpu) {
        const auto *generation = cpu->parent()->code_mapping_generation;
        return generation ? generation->load(std::memory_order_acquire) : 0;
    }
    static cpu_cache &cache(ARMul_State *cpu) {
        if (last && last->cpu == cpu) return *last;
        for (auto &item : caches) if (item->cpu == cpu) { last = item.get(); return *last; }
        if (caches.size() == 16) throw std::runtime_error("Counted CPU cache bound exceeded");
        caches.push_back(std::make_unique<cpu_cache>(cpu_cache{cpu, std::vector<stamp>(entries.size())}));
        last = caches.back().get();
        return *last;
    }
    static std::size_t find(ARMul_State *cpu) {
        if (cpu->TFlag || cpu->Reg[15] < first_pc || cpu->Reg[15] > last_pc) return entries.size();
        const auto key = cpu->make_instruction_cache_key(cpu->Reg[15]);
        if (key == last_key) return last_index;
        const auto found = indices.find(key);
        last_key = key; last_index = found == indices.end() ? entries.size() : found->second;
        return last_index;
    }
    void reset() {
        enabled = false; invoke = untracked = nullptr; tracking = false; decoding = nullptr; last = nullptr;
        caches.clear(); entries.clear(); first_pc = last_pc = 0;
        indices.clear(); last_key = ~std::uint64_t(0); last_index = 0;
        call_count = instruction_count = 0;
    }
    bool add(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
             std::size_t count, std::uint32_t function) {
        if (enabled || !words || !count || count > 128 || entries.size() == 4096
            || (pc & 3) || count * 4 > 4096 - (pc & 4095)) return false;
        for (const auto &item : entries) if (item.asid == asid && item.pc == pc) return false;
        indices.emplace((std::uint64_t(asid) << 32) | pc, entries.size());
        entries.push_back({asid, pc, function, std::vector<std::uint32_t>(words, words + count)});
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
        decoding = nullptr;
        if (!enabled || cpu->NumInstrsToExecute == 1) return;
        const auto index = find(cpu);
        if (index == entries.size()) return;
        decoding = cpu; decoding_entry = index; decoded = 0; matches = true; decoded_pointers.clear();
        decoding_mapping = mapping(cpu); decoding_generation = cpu->trans_gen;
    }
    void decoded_word(ARMul_State *cpu, std::uint32_t pc, std::uint32_t word, std::size_t ptr) {
        if (decoding != cpu) return;
        const auto &item = entries[decoding_entry];
        matches = matches && decoded < item.words.size()
            && pc == item.pc + decoded * 4 && word == item.words[decoded];
        if (decoded < item.words.size()) decoded_pointers.push_back(ptr);
        ++decoded;
    }
    void finish_decode(ARMul_State *cpu, std::size_t ptr) {
        if (decoding != cpu) return;
        auto &proof = cache(cpu).stamps[decoding_entry];
        const auto &item = entries[decoding_entry];
        core::code_mapping view;
        const bool mapped = cpu->parent()->resolve_code && cpu->parent()->resolve_code(item.pc, view)
            && view.bytes && view.size >= item.words.size() * 4 && view.address_space == item.asid
            && reinterpret_cast<std::uintptr_t>(view.bytes) <= 0xffffffffu - item.words.size() * 4;
        proof = {matches && decoded == item.words.size() && !cpu->TFlag
            && cpu->instruction_cache_asid == item.asid && cpu->NumInstrsToExecute
            && decoding_generation == cpu->trans_gen && decoding_mapping == mapping(cpu) && mapped,
            cpu->trans_gen, decoding_mapping, ptr,
            mapped ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(view.bytes)) : 0, decoded_pointers};
        decoding = nullptr;
    }
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix) {
        if (!enabled) return 0;
        const auto index = find(cpu);
        if (index == entries.size()) return 0;
        const auto &item = entries[index];
        if (cpu->NumInstrsToExecute < prefix || cpu->NumInstrsToExecute - prefix < item.words.size()) return 0;
        auto &owner = cache(cpu);
        owner.pending_resume = false;
        auto &proof = owner.stamps[index];
        if (!proof.accepted || proof.generation != cpu->trans_gen) return 0;
        const auto current_mapping = mapping(cpu);
        if (proof.mapping != current_mapping) {
            core::code_mapping view;
            const bool same = cpu->parent()->resolve_code && cpu->parent()->resolve_code(item.pc, view)
                && view.address_space == item.asid && view.size >= item.words.size() * 4
                && reinterpret_cast<std::uintptr_t>(view.bytes) == proof.host;
            if (!same) { proof.accepted = false; return 0; }
            // Unrelated mappings do not invalidate this existing decoded view.
            // Revalidate only its physical identity, once per mapping generation.
            proof.mapping = current_mapping;
        }
        // The stamp was installed only with this original decoded entry. Cache
        // clears bump trans_gen; raw writes without IMB retain the same decoded
        // instruction visibility in both routes, without re-reading new bytes.
        const auto key = cpu->make_instruction_cache_key(cpu->Reg[15]);
        const auto &slot = cpu->block_l1_cache[ARMul_State::block_l1_index(key)];
        if (slot.key != key || slot.ptr != proof.ptr) return 0;
        const auto remaining = cpu->NumInstrsToExecute - prefix;
        auto *tlb = cpu->mem_cache_;
        cpu->aot_tlb = tlb && tlb->page_bits == 12
            ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(tlb->entries)) : 0;
        cpu->aot_exit = 0;
        cpu->aot_code_begin = proof.host; cpu->aot_code_end = proof.host + item.words.size() * 4;
        cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
            | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
        const auto count = invoke(cpu, item.function);
        if (!count)
            throw std::runtime_error("Counted executor returned zero after admission");
        if (count > item.words.size() || count > remaining)
            throw std::runtime_error("Counted executor exceeded admitted block");
        if (count && count < item.words.size() && cpu->NumInstrsToExecute) {
            if (cpu->Reg[15] != item.pc + count * 4 || proof.instructions.size() != item.words.size())
                throw std::runtime_error("Unexpected counted partial boundary");
            owner.pending_resume = true; owner.resume_ptr = proof.instructions[count];
        }
        cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
            | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
        return count | (count == item.words.size() ? 0x100u : 0u);
    }
    bool resume(ARMul_State *cpu, std::size_t &ptr) {
        auto &owner = cache(cpu);
        if (!owner.pending_resume) return false;
        owner.pending_resume = false; ptr = owner.resume_ptr; return true;
    }
    void forget(ARMul_State *cpu) {
        if (decoding == cpu) decoding = nullptr;
        if (last && last->cpu == cpu) last = nullptr;
        caches.erase(std::remove_if(caches.begin(), caches.end(),
            [cpu](const auto &item) { return item->cpu == cpu; }), caches.end());
    }
    std::uint64_t calls() { return call_count; }
    std::uint64_t instructions() { return instruction_count; }
    bool diagnostics() { return tracking; }
}
