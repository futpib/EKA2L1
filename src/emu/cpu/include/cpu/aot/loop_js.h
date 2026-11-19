#pragma once
#include <cpu/aot/counted_js.h>
#include <cstdint>
struct ARMul_State;
namespace eka2l1::arm::loop_js {
    inline constexpr unsigned progress_limit = 65535;
    inline constexpr unsigned complete_bit = 65536;
    struct invocation_context {
        std::uint32_t budget, generation, mapping, asid, l1_key, l1_pointer, tlb;
    };
    using executor = std::uint32_t (*)(ARMul_State *, std::uint32_t function,
                                      const invocation_context *context);
    extern bool enabled;
#if defined(EKA2L1_PACKED_PIXEL_JS)
    struct packed_pixel_pattern;
    extern bool packed_pixel_enabled;
    int configure_packed_pixel(bool enabled);
    std::uint8_t prepare_packed_pixel(const packed_pixel_pattern &pattern);
    std::uint64_t packed_pixel_calls();
    std::uint64_t packed_pixel_iterations();
    std::uint64_t packed_pixel_declines();
#endif
    void reset();
    void install(executor execute, bool diagnostics = false);
    // Exact cumulative original count, at an original complete block boundary.
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix);
    // Explicit v2 progress for memory prefixes; never returns zero after effects.
    std::uint32_t execute_progress(ARMul_State *cpu, std::uint32_t prefix);
    std::uint32_t execute_admitted(ARMul_State *cpu, std::uint32_t prefix,
                                   const counted_js::loop_admission &proof);
    std::uint64_t calls();
    std::uint64_t instructions();
}
