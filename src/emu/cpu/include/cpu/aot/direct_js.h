#pragma once
#include <cstdint>

struct ARMul_State;
namespace eka2l1::arm::direct_js {
    inline constexpr std::uint32_t progress_limit = 65535, complete_bit = 65536;
    inline constexpr std::uint32_t block_limit = 4096;
    struct invocation_context {
        std::uint32_t stamps, stamp_stride, entry_count, start_index, budget, prefix;
        std::uint32_t generation, mapping, asid, tlb_pointer, l1, l1_stride, l1_pointer_offset;
        std::uint32_t quantum, irq, cpsr, tflag;
        std::uint32_t aot_tlb, code_begin, code_end, aot_exit;
        std::uint32_t epoch_pointer, epoch;
        std::uint32_t entries, entry_stride, loop_offset, region_offset;
        std::uint32_t last_index, last_prefix, blocks;
    };
    static_assert(sizeof(invocation_context) == 120);
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
