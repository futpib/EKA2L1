#include <cpu/aot/loop_js.h>
#include <cpu/aot/counted_js.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/arm_interface.h>
#include <algorithm>
#include <stdexcept>

namespace eka2l1::arm::loop_js {
    bool enabled = false;
    static executor invoke = nullptr;
    static bool tracking = false;
    static std::uint64_t call_count = 0, instruction_count = 0;
    void reset() {
        enabled = tracking = false; invoke = nullptr;
        call_count = instruction_count = 0;
        counted_js::clear_loops();
    }
    void install(executor function, bool diagnostics) {
        if (enabled || !function || !counted_js::enabled) throw std::runtime_error("Invalid loop installation");
        invoke = function; tracking = diagnostics; enabled = true;
    }
    static void publish(ARMul_State *cpu) {
        cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
            | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
    }
    std::uint32_t execute_admitted(ARMul_State *cpu, std::uint32_t prefix,
            const counted_js::loop_admission &proof) {
        if (!enabled || !invoke || cpu->TFlag || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return 0;
        auto *mapping = cpu->parent()->code_mapping_generation;
        if (proof.word_memory && !mapping) return 0;
        const auto remaining = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            cpu->NumInstrsToExecute - prefix, progress_limit));
        const auto key = cpu->make_instruction_cache_key(cpu->Reg[15]);
        const auto &slot = cpu->block_l1_cache[ARMul_State::block_l1_index(key)];
        const auto address = [](const auto *value) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
        };
        const invocation_context context{remaining, address(&cpu->trans_gen), address(mapping),
            address(&cpu->instruction_cache_asid), address(&slot.key), address(&slot.ptr), address(&cpu->mem_cache_)};
        auto *tlb = cpu->mem_cache_;
        cpu->aot_tlb = tlb && tlb->page_bits == 12 ? address(tlb->entries) : 0;
        cpu->aot_code_begin = proof.host; cpu->aot_code_end = proof.host + proof.words * 4u;
        publish(cpu); cpu->aot_exit = 0;
        const auto result = invoke(cpu, proof.function, &context);
        // A function may have performed effects. Invalid progress never retries.
        if (result & ~(progress_limit | complete_bit))
            throw std::runtime_error("Invalid counted loop progress bits");
        const auto count = proof.word_memory ? result & progress_limit : result;
        const bool complete = !proof.word_memory || (result & complete_bit);
        if (!count || count > remaining || cpu->TFlag
            || (complete ? count % proof.words != 0
                || (cpu->Reg[15] != proof.pc && cpu->Reg[15] != proof.pc + proof.words * 4u)
                : count % proof.words == 0 || cpu->Reg[15] != proof.pc + (count % proof.words) * 4u))
            throw std::runtime_error("Invalid counted loop progress after admission");
        if (!complete && cpu->NumInstrsToExecute) counted_js::resume_loop(cpu, proof, count % proof.words);
        publish(cpu);
        if (tracking) { ++call_count; instruction_count += count; }
        return count | (complete ? complete_bit : 0u);
    }
    std::uint32_t execute_progress(ARMul_State *cpu, std::uint32_t prefix) {
        if (!enabled || cpu->TFlag || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return 0;
        counted_js::loop_admission proof;
        if (!counted_js::admit_loop(cpu, prefix, proof)) return 0;
        return execute_admitted(cpu, prefix, proof);
    }
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix) {
        const auto result = execute_progress(cpu, prefix);
        if (result && !(result & complete_bit)) throw std::runtime_error("Partial loop requires progress ABI");
        return result & progress_limit;
    }
    std::uint64_t calls() { return call_count; }
    std::uint64_t instructions() { return instruction_count; }
}
