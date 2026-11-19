#pragma once
#include <cstddef>
#include <cstdint>

struct ARMul_State;
namespace eka2l1::arm::word_increment {
    extern bool enabled;
    void reset();
    void install(bool diagnostics = false);
    void begin_decode(ARMul_State *cpu);
    void decoded_word(ARMul_State *cpu, std::uint32_t pc, std::uint32_t word);
    void finish_decode(ARMul_State *cpu, std::size_t pointer);
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix);
    void forget(ARMul_State *cpu);
    std::uint64_t calls();
    std::uint64_t iterations();
    std::uint64_t declines();
}
