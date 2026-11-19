#pragma once
#include <cpu/aot/counted_js.h>
#include <array>
#include <cstddef>
#include <cstdint>

struct ARMul_State;
namespace eka2l1::arm::region_js {
    inline constexpr unsigned member_limit = 8;
    inline constexpr unsigned progress_limit = 65535;
    inline constexpr unsigned complete_bit = 65536;
    inline constexpr unsigned member_shift = 17;
    inline constexpr unsigned prefix_shift = 20;
    struct member_context {
        std::uint32_t l1_key, l1_pointer, decoded_pointer, host_begin, host_end;
    };
    struct invocation_context {
        std::uint32_t budget, generation, mapping, asid, tlb, ready, start;
        std::array<member_context, member_limit> members;
    };
    static_assert(sizeof(invocation_context) == 188);
    struct member {
        std::uint32_t asid, pc;
        const std::uint32_t *words;
        std::size_t count;
    };
    using executor = std::uint32_t (*)(ARMul_State *, std::uint32_t,
                                      const invocation_context *);
    extern bool enabled;
    void reset();
    void install(executor execute, bool diagnostics = false);
    bool bind(const member *members, std::size_t count, std::uint32_t function);
    // Internal common-admission handoff; progress is validated by counted_js.
    std::uint32_t invoke_admitted(ARMul_State *cpu, std::uint32_t function,
                                  const invocation_context &context);
    void record(std::uint32_t count);
    std::uint64_t calls();
    std::uint64_t instructions();
}
