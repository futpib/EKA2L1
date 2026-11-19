#include <cpu/aot/word_increment.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/arm_interface.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace eka2l1::arm::word_increment {
    // Descending indexed LDR/ADD/STR/SUBS/BPL; registers are derived from
    // the complete original body. No title, address or module is recognized.
    static constexpr unsigned body_words = 5;
    static bool pattern(const std::array<std::uint32_t, body_words> &words,
            unsigned &base, unsigned &counter, unsigned &temporary) {
        base = (words[0] >> 16) & 15u;
        temporary = (words[0] >> 12) & 15u;
        counter = words[0] & 15u;
        if (base > 12 || counter > 12 || temporary > 12
            || base == counter || base == temporary || counter == temporary) return false;
        return (words[0] & 0xfff00ff0u) == 0xe7900100u
            && words[1] == (0xe2800001u | temporary << 16 | temporary << 12)
            && words[2] == (0xe7800100u | base << 16 | temporary << 12 | counter)
            && words[3] == (0xe2500001u | counter << 16 | counter << 12)
            && words[4] == 0x5afffffau;
    }
    struct stamp {
        bool valid = false;
        std::uint32_t asid = 0, pc = 0, generation = 0;
        std::uint64_t mapping = 0;
        std::size_t pointer = 0;
        const std::uint8_t *host = nullptr;
        unsigned base = 0, counter = 0, temporary = 0;
    };
    struct cpu_cache {
        ARMul_State *cpu = nullptr;
        std::array<stamp, 64> slots{};
    };
    static std::array<cpu_cache, 16> caches{};
    static cpu_cache *last_cache = nullptr;
    static bool has_entries = false;
    static std::uint32_t first_pc = 0, last_pc = 0;
    static ARMul_State *decoding = nullptr;
    static stamp pending;
    static unsigned decoded = 0;
    static bool matches = false, tracking = false;
    static std::array<std::uint32_t, body_words> pending_words{};
    static std::uint64_t call_count = 0, iteration_count = 0, decline_count = 0;
    bool enabled = false;

    static std::uint64_t mapping(ARMul_State *cpu) {
        const auto *generation = cpu->parent()->code_mapping_generation;
        return generation ? generation->load(std::memory_order_acquire) : 0;
    }
    static cpu_cache *cache(ARMul_State *cpu, bool create) {
        if (last_cache && last_cache->cpu == cpu) return last_cache;
        cpu_cache *empty = nullptr;
        for (auto &item : caches) {
            if (item.cpu == cpu) { last_cache = &item; return &item; }
            if (!item.cpu && !empty) empty = &item;
        }
        if (create && empty) { empty->cpu = cpu; last_cache = empty; return empty; }
        return nullptr;
    }
    static std::size_t index(std::uint32_t asid, std::uint32_t pc) {
        return ((pc >> 2) ^ (pc >> 12) ^ asid) & 63u;
    }
    static bool overlaps(std::uintptr_t a, std::size_t an, std::uintptr_t b, std::size_t bn) {
        return a < b + bn && b < a + an;
    }
    void reset() {
        enabled = tracking = false; decoding = nullptr; last_cache = nullptr;
        has_entries = false; first_pc = last_pc = 0;
        for (auto &item : caches) {
            item.cpu = nullptr;
            for (auto &slot : item.slots) slot.valid = false;
        }
        call_count = iteration_count = decline_count = 0;
    }
    void install(bool diagnostics) { reset(); tracking = diagnostics; enabled = true; }
    void begin_decode(ARMul_State *cpu) {
        decoding = nullptr;
        if (!enabled || cpu->TFlag || cpu->NumInstrsToExecute == 1) return;
        decoding = cpu; decoded = 0; matches = true;
        pending = {false, cpu->instruction_cache_asid, cpu->Reg[15], cpu->trans_gen, mapping(cpu)};
    }
    void decoded_word(ARMul_State *cpu, std::uint32_t pc, std::uint32_t word) {
        if (decoding != cpu) return;
        matches = matches && decoded < body_words && pc == pending.pc + decoded * 4;
        if (decoded < body_words) pending_words[decoded] = word;
        ++decoded;
    }
    void finish_decode(ARMul_State *cpu, std::size_t pointer) {
        if (decoding != cpu) return;
        decoding = nullptr;
        if (!matches || decoded != body_words || cpu->TFlag || !cpu->NumInstrsToExecute
            || cpu->instruction_cache_asid != pending.asid || cpu->trans_gen != pending.generation
            || mapping(cpu) != pending.mapping) return;
        if (!pattern(pending_words, pending.base, pending.counter, pending.temporary)) return;
        core::code_mapping view;
        if (!cpu->parent()->resolve_code || !cpu->parent()->resolve_code(pending.pc, view)
            || view.address_space != pending.asid || !view.bytes || view.size < body_words * 4) return;
        auto *owner = cache(cpu, true);
        if (!owner) return;
        pending.valid = true; pending.pointer = pointer; pending.host = view.bytes;
        owner->slots[index(pending.asid, pending.pc)] = pending;
        first_pc = has_entries ? std::min(first_pc, pending.pc) : pending.pc;
        last_pc = has_entries ? std::max(last_pc, pending.pc) : pending.pc;
        has_entries = true;
    }
#ifdef __EMSCRIPTEN__
    EM_JS(void, increment_words, (std::uint8_t *destination, unsigned words), {
        var index = destination >>> 2;
        for (var i = words - 1; i >= 0; --i) {
            HEAPU32[index + i] = (HEAPU32[index + i] + 1) >>> 0;
        }
    });
#else
    static void increment_words(std::uint8_t *destination, unsigned words) {
        for (unsigned i = words; i-- > 0;) {
            std::uint32_t value;
            std::memcpy(&value, destination + i * 4, sizeof(value));
            ++value;
            std::memcpy(destination + i * 4, &value, sizeof(value));
        }
    }
#endif
    static std::uint32_t decline() {
        if (tracking) ++decline_count;
        return 0;
    }
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix) {
        if (!enabled || !has_entries || cpu->TFlag
            || cpu->Reg[15] < first_pc || cpu->Reg[15] > last_pc) return 0;
        auto *owner = cache(cpu, false);
        if (!owner) return 0;
        auto &proof = owner->slots[index(cpu->instruction_cache_asid, cpu->Reg[15])];
        if (!proof.valid || proof.pc != cpu->Reg[15] || proof.asid != cpu->instruction_cache_asid
            || proof.generation != cpu->trans_gen) return 0;
        const auto key = cpu->make_instruction_cache_key(cpu->Reg[15]);
        const auto &original = cpu->block_l1_cache[ARMul_State::block_l1_index(key)];
        if (original.key != key || original.ptr != proof.pointer) return decline();
        if (proof.mapping != mapping(cpu)) {
            core::code_mapping view;
            if (!cpu->parent()->resolve_code || !cpu->parent()->resolve_code(proof.pc, view)
                || view.address_space != proof.asid || view.bytes != proof.host || view.size < body_words * 4) {
                proof.valid = false; return decline();
            }
            proof.mapping = mapping(cpu);
        }
        if (cpu->NumInstrsToExecute <= prefix || cpu->InBigEndianMode()
            || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return decline();
        const auto before = cpu->Reg[proof.counter];
        auto iterations = static_cast<unsigned>(std::min<std::uint64_t>((cpu->NumInstrsToExecute - prefix) / 5, 64));
        // BPL tests N, including INT_MIN -> INT_MAX with V set. Other
        // negative starting indices terminate after exactly one iteration.
        if (before < 0x80000000u) iterations = std::min<std::uint64_t>(iterations, std::uint64_t(before) + 1);
        else if (before != 0x80000000u) iterations = std::min(iterations, 1u);
        const std::uint32_t address = cpu->Reg[proof.base] + (before << 2);
        if (address & 3u) return decline();
        // Original effective addressing is modulo32. Only the selected
        // descending physical span must avoid a word/page/address wrap.
        iterations = std::min(iterations, ((address & 4095u) + 4u) / 4u);
        if (!iterations || address > 0xfffffffcu || address < (iterations - 1u) * 4u) return decline();
        const auto bytes = iterations * 4u, first = address - (iterations - 1u) * 4u;
        auto *tlb = cpu->mem_cache_;
        if (!tlb || tlb->page_bits != 12) return decline();
        auto *read = tlb->lookup_access<prot_read>(first);
        auto *write = tlb->lookup_access<prot_write>(first);
        if (!read || !write || read != write) return decline();
        const auto host = reinterpret_cast<std::uintptr_t>(write);
        if ((host & 3u) || host > std::numeric_limits<std::uintptr_t>::max() - bytes
            || overlaps(host, bytes, reinterpret_cast<std::uintptr_t>(proof.host), body_words * 4)
            || overlaps(host, bytes, reinterpret_cast<std::uintptr_t>(cpu), sizeof(*cpu))) return decline();
        const auto &page = tlb->entries[tlb->index(first)];
        if (page.execute_addr == (first & ~4095u)) return decline();
        // Distinct registers and one ordinary read/write mapping preserve
        // descending load/add/store order. No callback can occur in the span.
        std::uint32_t last_value;
        std::memcpy(&last_value, read, sizeof(last_value));
        increment_words(write, iterations);
        cpu->Reg[proof.temporary] = last_value + 1u;
        const auto last_before = before - iterations + 1u;
        cpu->Reg[proof.counter] = before - iterations;
        cpu->shifter_carry_out = iterations == 1 ? cpu->CFlag : (last_before + 1u != 0);
        cpu->NFlag = cpu->Reg[proof.counter] >> 31;
        cpu->ZFlag = cpu->Reg[proof.counter] == 0;
        cpu->CFlag = last_before != 0;
        cpu->VFlag = last_before == 0x80000000u;
        cpu->Reg[15] = proof.pc + (cpu->NFlag ? body_words * 4u : 0u);
        if (tracking) { ++call_count; iteration_count += iterations; }
        return iterations * 5u;
    }
    void forget(ARMul_State *cpu) {
        if (decoding == cpu) decoding = nullptr;
        if (last_cache && last_cache->cpu == cpu) last_cache = nullptr;
        if (auto *owner = cache(cpu, false)) {
            if (last_cache == owner) last_cache = nullptr;
            owner->cpu = nullptr;
            for (auto &slot : owner->slots) slot.valid = false;
        }
    }
    std::uint64_t calls() { return call_count; }
    std::uint64_t iterations() { return iteration_count; }
    std::uint64_t declines() { return decline_count; }
}
