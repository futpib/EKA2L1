#pragma once
#include <cstddef>
#include <cstdint>

struct ARMul_State;
namespace eka2l1::arm::direct_js { struct configuration_layout; struct invocation_context; }
namespace eka2l1::arm::region_js { struct member; }
namespace eka2l1::arm::loop_js { struct packed_pixel_pattern; struct palette_pixel_pattern; struct blend_pixel_pattern; }
namespace eka2l1::arm::counted_js {
    inline constexpr unsigned entry_limit = 5120;
    using executor = std::uint32_t (*)(ARMul_State *, std::uint32_t);
    extern bool enabled;
    void reset();
#if defined(EKA2L1_COUNTED_DIRECT_JS)
    direct_js::configuration_layout direct_configuration();
#if defined(EKA2L1_COUNTED_WASM)
    std::uint32_t execute_native_direct(ARMul_State *cpu, direct_js::invocation_context *context);
#endif
#endif
    bool add(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
             std::size_t count, std::uint32_t function, bool shared = false, bool thumb = false);
    void install(executor execute, bool diagnostics = false);
    // Alternate pure-loop functions reuse the existing immutable candidate and
    // actual-ASID original-decoder stamps; no second cache owns code visibility.
    struct loop_admission {
        std::uint32_t pc, words, function, host;
        bool word_memory;
        const std::size_t *instructions;
        bool semantic = false;
#if defined(EKA2L1_PACKED_PIXEL_JS)
        const loop_js::packed_pixel_pattern *packed_pixel = nullptr;
        const loop_js::palette_pixel_pattern *palette_pixel = nullptr;
        const loop_js::blend_pixel_pattern *blend_pixel = nullptr;
#endif
    };
    bool bind_loop(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
                   std::size_t count, std::uint32_t function);
    bool bind_word_loop(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
                        std::size_t count, std::uint32_t function);
    bool bind_semantic(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
                       std::size_t count, std::uint32_t function);
    void clear_semantics();
    void clear_loops();
    bool admit_loop(ARMul_State *cpu, std::uint32_t prefix, loop_admission &result);
    void resume_loop(ARMul_State *cpu, const loop_admission &proof, std::uint32_t remainder);
    // One lookup/admission for either existing generic or loop executor.
    // Low16 count, bit16 completed original block boundary.
    std::uint32_t execute_dispatch(ARMul_State *cpu, std::uint32_t prefix);
    void begin_decode(ARMul_State *cpu);
    void decoded_word(ARMul_State *cpu, std::uint32_t pc, std::uint32_t word, std::size_t ptr);
    void finish_decode(ARMul_State *cpu, std::size_t ptr);
    // Low byte: exact prefix count. Bit 8: original block boundary reached.
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix);
    bool resume(ARMul_State *cpu, std::size_t &ptr);
    void forget(ARMul_State *cpu);
    std::uint64_t calls();
    std::uint64_t instructions();
    bool diagnostics();
    bool bind_region(const region_js::member *members, std::size_t count, std::uint32_t function);
    void clear_regions();
}
