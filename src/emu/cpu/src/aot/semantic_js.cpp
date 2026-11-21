#include <cpu/aot/semantic_js.h>
#include <cpu/arm_interface.h>
#include <cpu/dyncom/armstate.h>
#include <common/code_tracking.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace eka2l1::arm::semantic_js {
    struct source_record {
        std::uint32_t pc;
        std::vector<std::uint32_t> words;
    };
    struct record {
        std::uint32_t asid, pc, function;
        std::vector<source_record> sources;
        executor invoke;
        bool diagnostics;
    };
    struct physical_span { std::uintptr_t begin; std::size_t bytes; };
    struct proof {
        ARMul_State *cpu = nullptr;
        std::size_t record_index = 0;
        std::uint32_t generation = 0;
        std::uint64_t mapping = 0;
        bool valid = false;
        std::vector<physical_span> code;
    };
    static std::vector<record> records;
    static std::array<proof, 8> proofs;
    static std::uint64_t authentication_count = 0, resolution_count = 0, resolved_page_count = 0;
    static ARMul_State *active = nullptr;
    static proof *active_proof = nullptr;
    static std::uint64_t mapping(ARMul_State *cpu) {
        const auto *generation = cpu->parent()->code_mapping_generation;
        return generation ? generation->load(std::memory_order_acquire) : 0;
    }
    static bool overlaps(std::uintptr_t a, std::size_t an, std::uintptr_t b, std::size_t bn) {
        return an && bn && a <= std::numeric_limits<std::uintptr_t>::max() - an
            && b <= std::numeric_limits<std::uintptr_t>::max() - bn && a < b + bn && b < a + an;
    }
    void reset() {
        if (active) throw std::runtime_error("Semantic installation during execution");
        records.clear();
        authentication_count = resolution_count = resolved_page_count = 0;
        for (auto &item : proofs) item = {};
    }
    bool install(std::uint32_t asid, std::uint32_t pc, std::uint32_t function,
            const source_span *sources, std::size_t count, executor invoke, bool diagnostics) {
        if (active || std::any_of(proofs.begin(), proofs.end(), [](const auto &item) { return item.cpu != nullptr; })
            || !invoke || !sources || !count || count > 64 || records.size() == 8
            || (pc & 3u) || function >= function_bit) return false;
        for (const auto &item : records) if (item.function == function || (item.asid == asid && item.pc == pc)) return false;
        record item{asid, pc, function, {}, invoke, diagnostics};
        std::size_t words = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto &source = sources[i];
            if (!source.words || !source.count || source.count > 4096 || (source.pc & 3u)
                || source.count > (std::uint64_t(0x100000000ull) - source.pc) / 4
                || (words += source.count) > 16384) return false;
            item.sources.push_back({source.pc, {source.words, source.words + source.count}});
        }
        records.push_back(std::move(item));
        return true;
    }
    static bool authenticate(ARMul_State *cpu, const record &item, proof &accepted) {
        accepted.valid = false; accepted.code.clear();
        if (item.diagnostics) ++authentication_count;
        const auto before_mapping = mapping(cpu), before_generation = std::uint64_t(cpu->trans_gen);
        const auto before_asid = cpu->instruction_cache_asid;
        auto *core = cpu->parent();
        if (!core->resolve_code || !core->resolve_data) return false;
        for (const auto &source : item.sources) {
            auto pc = source.pc;
            std::size_t offset = 0, bytes = source.words.size() * 4;
            while (offset < bytes) {
                core::code_mapping view;
                if (!core->resolve_code(pc, view) || view.address_space != item.asid
                    || !view.bytes || !view.size) return false;
                const auto take = std::min(view.size, bytes - offset);
                if (std::memcmp(view.bytes, reinterpret_cast<const std::uint8_t *>(source.words.data()) + offset, take)) return false;
                accepted.code.push_back({reinterpret_cast<std::uintptr_t>(view.bytes), take});
                offset += take; pc += static_cast<std::uint32_t>(take);
            }
        }
        if (before_mapping != mapping(cpu) || before_generation != cpu->trans_gen
            || before_asid != cpu->instruction_cache_asid) return false;
        accepted.mapping = before_mapping; accepted.generation = static_cast<std::uint32_t>(before_generation); accepted.valid = true;
        return true;
    }
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t budget, std::uint32_t function) {
#ifdef EKA2L1_MEMORY_ACCESS_PROFILE
        return 0;
#endif
        if (active || !budget || cpu->TFlag || cpu->InBigEndianMode()
            || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))
            || !common::code_tracking::skip_code_scans() || !mapping(cpu)) return 0;
        const auto found = std::find_if(records.begin(), records.end(), [&](const auto &item) {
            return item.function == function && item.asid == cpu->instruction_cache_asid && item.pc == cpu->Reg[15];
        });
        if (found == records.end()) return 0;
        const auto index = static_cast<std::size_t>(found - records.begin());
        auto accepted = std::find_if(proofs.begin(), proofs.end(), [&](const auto &item) { return item.cpu == cpu && item.record_index == index; });
        if (accepted == proofs.end()) {
            accepted = std::find_if(proofs.begin(), proofs.end(), [](const auto &item) { return !item.cpu; });
            if (accepted == proofs.end()) return 0;
            accepted->cpu = cpu; accepted->record_index = index;
        }
        if ((!accepted->valid || accepted->mapping != mapping(cpu) || accepted->generation != cpu->trans_gen)
            && !authenticate(cpu, *found, *accepted)) return 0;
        struct scope {
            scope(ARMul_State *cpu, proof *item) { active = cpu; active_proof = item; }
            ~scope() { active = nullptr; active_proof = nullptr; }
        } executing(cpu, &*accepted);
        const auto count = found->invoke(cpu, function, budget);
        if (count > budget || (count && (cpu->Reg[15] != found->pc || cpu->TFlag
            || cpu->trans_gen != accepted->generation || mapping(cpu) != accepted->mapping)))
            throw std::runtime_error("Invalid semantic original boundary");
        return count;
    }
    std::uintptr_t resolve(ARMul_State *cpu, std::uint32_t address, std::uint32_t bytes, bool write) {
        if (active != cpu || !active_proof || !bytes || std::uint64_t(address) + bytes > 0x100000000ull
            || !cpu->parent()->resolve_data || mapping(cpu) != active_proof->mapping) return 0;
        const auto diagnostics = records[active_proof->record_index].diagnostics;
        if (diagnostics) ++resolution_count;
        std::uintptr_t first = 0;
        std::size_t offset = 0;
        while (offset < bytes) {
            if (diagnostics) ++resolved_page_count;
            core::data_mapping view;
            if (!cpu->parent()->resolve_data(address + static_cast<std::uint32_t>(offset), write, view)
                || view.address_space != cpu->instruction_cache_asid || !view.bytes || !view.size) return 0;
            const auto host = reinterpret_cast<std::uintptr_t>(view.bytes), take = std::min(view.size, bytes - offset);
            if (!offset) {
                if (host > std::numeric_limits<std::uintptr_t>::max() - bytes) return 0;
                first = host;
            }
            if (host != first + offset || overlaps(host, take, reinterpret_cast<std::uintptr_t>(cpu), sizeof(*cpu))
                || overlaps(host, take, reinterpret_cast<std::uintptr_t>(cpu->parent()), sizeof(core))
                || overlaps(host, take, reinterpret_cast<std::uintptr_t>(records.data()), records.capacity() * sizeof(record))
                || overlaps(host, take, reinterpret_cast<std::uintptr_t>(proofs.data()), sizeof(proofs))) return 0;
            for (const auto &item : records) {
                if (overlaps(host, take, reinterpret_cast<std::uintptr_t>(item.sources.data()), item.sources.capacity() * sizeof(source_record))) return 0;
                for (const auto &source : item.sources)
                    if (overlaps(host, take, reinterpret_cast<std::uintptr_t>(source.words.data()), source.words.capacity() * 4)) return 0;
            }
            for (const auto &item : proofs) {
                if (overlaps(host, take, reinterpret_cast<std::uintptr_t>(item.code.data()), item.code.capacity() * sizeof(physical_span))) return 0;
                for (const auto &source : item.code) if (overlaps(host, take, source.begin, source.bytes)) return 0;
            }
            offset += take;
        }
        if (mapping(cpu) != active_proof->mapping || cpu->trans_gen != active_proof->generation) return 0;
        return first;
    }
    std::uint64_t authentications() { return authentication_count; }
    std::uint64_t resolutions() { return resolution_count; }
    std::uint64_t resolved_pages() { return resolved_page_count; }
    void forget(ARMul_State *cpu) {
        if (active == cpu) throw std::runtime_error("Retiring active semantic CPU");
        for (auto &item : proofs) if (item.cpu == cpu) item = {};
    }
}
