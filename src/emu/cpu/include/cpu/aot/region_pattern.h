#pragma once
#include <cstddef>
#include <cstdint>
namespace eka2l1::arm::region_js {
    inline bool block_words(const std::uint32_t *words, std::size_t count, std::uint32_t pc) {
        if (!words || !count || count > 128 || (pc & 3) || count * 4 > 4096 - (pc & 4095)) return false;
        for (std::size_t n = 0; n < count; ++n) {
            const auto op = words[n];
            if ((op >> 28) > 14) return false;
            if (n + 1 == count) return (op & 0x0e000000u) == 0x0a000000u
                || ((op & 0x0ffffff0u) == 0x012fff10u && (op & 15u) != 15u);
            const unsigned alu = (op >> 21) & 15, rn = (op >> 16) & 15, rd = (op >> 12) & 15;
            if ((op & 0x0c000000u) == 0x04000000u) {
                if ((op & (1u << 22)) || rn == 15 || rd == 15
                    || ((op & (1u << 20)) && (!(op & (1u << 24)) || (op & (1u << 21))) && rn == rd)
                    || ((op & (1u << 25)) && ((op & 16u) || (op & 15u) == 15u
                        || (op & 15u) == rn || (op & 15u) == rd))) return false;
                continue;
            }
            if ((op & 0x0c000000u) || rn == 15 || rd == 15
                || (alu >= 8 && alu <= 11 && !(op & (1u << 20)))) return false;
            if (!(op & (1u << 25)) && ((op & 0x90u) == 0x90u || (op & 15u) == 15u
                || ((op & 16u) && ((op >> 8) & 15u) == 15u))) return false;
        }
        return false;
    }
}
