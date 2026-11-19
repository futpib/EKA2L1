#pragma once
#include <cstdint>

struct ARMul_State;
namespace eka2l1::arm::direct_js {
    inline constexpr std::uint32_t progress_limit = 65535, complete_bit = 65536;
    inline constexpr std::uint32_t block_limit = 4096;
    struct configuration_layout {
        std::uint32_t entries, entry_count, entry_stride, loop_offset, region_offset, stamp_stride;
        std::uint32_t cpu_size, pc_offset, generation_offset, asid_offset, tlb_pointer_offset;
        std::uint32_t l1_offset, l1_stride, l1_pointer_offset, quantum_offset, irq_offset;
        std::uint32_t cpsr_offset, tflag_offset, aot_tlb_offset, code_begin_offset, code_end_offset, aot_exit_offset;
        std::uint32_t epoch_pointer;
    };
    static_assert(sizeof(configuration_layout) == 92);
    struct invocation_context {
        std::uint32_t stamps, mapping, start_index, budget, prefix, epoch;
        std::uint32_t last_index, last_prefix, blocks;
    };
    static_assert(sizeof(invocation_context) == 36);
    using executor = std::uint32_t (*)(ARMul_State *, invocation_context *);
    extern bool enabled;
    void reset();
    void install(executor function, bool diagnostics = false);
    void require_idle();
    void invalidate();
    std::uint32_t *epoch_address();
    std::uint32_t invoke(ARMul_State *cpu, invocation_context *context);
    void record(std::uint32_t instructions, std::uint32_t blocks);
    std::uint64_t calls();
    std::uint64_t instructions();
    std::uint64_t blocks();
}
