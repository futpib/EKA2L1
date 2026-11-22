#pragma once
#include <cstddef>
#include <cstdint>

struct ARMul_State;
namespace eka2l1::arm::semantic_js {
    inline constexpr std::uint32_t function_bit = 0x40000000u;
    struct source_span {
        std::uint32_t pc;
        const std::uint32_t *words;
        std::size_t count;
    };
    using executor = std::uint32_t (*)(ARMul_State *, std::uint32_t, std::uint32_t);
    // Installation copies immutable original source words. Native/reference
    // execution remains unchanged unless a selected entry is explicitly bound.
    bool install(std::uint32_t asid, std::uint32_t pc, std::uint32_t function,
                 const source_span *sources, std::size_t count, executor invoke, bool diagnostics = false, bool original_fallback = false);
    // Complete-loop helpers can decline without running an ordinary AOT block.
    bool original_fallback(std::uint32_t function);
    void reset();
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t budget, std::uint32_t function);
    // Exact contiguous callback-free RAM; zero declines before guest effects.
    std::uintptr_t resolve(ARMul_State *cpu, std::uint32_t address,
                           std::uint32_t bytes, bool write);
    void forget(ARMul_State *cpu);
    std::uint64_t authentications();
    std::uint64_t resolutions();
    std::uint64_t resolved_pages();
}
