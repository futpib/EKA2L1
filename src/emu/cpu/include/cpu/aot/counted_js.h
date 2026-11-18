#pragma once
#include <cstddef>
#include <cstdint>

struct ARMul_State;
namespace eka2l1::arm::counted_js {
    using executor = std::uint32_t (*)(ARMul_State *, std::uint32_t);
    extern bool enabled;
    void reset();
    bool add(std::uint32_t asid, std::uint32_t pc, const std::uint32_t *words,
             std::size_t count, std::uint32_t function);
    void install(executor execute, bool diagnostics = false);
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
}
