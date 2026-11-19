#pragma once
#include <cstddef>
#include <cstdint>
namespace eka2l1::arm::loop_js {
    inline bool original_loop_words(const std::uint32_t *words, std::size_t count,
            std::uint32_t pc, bool word_memory) {
        if (!words || count < 2 || count > 128 || (pc & 3) || count * 4 > 4096 - (pc & 4095)) return false;
        for (std::size_t n = 0; n < count; ++n) {
            const auto op = words[n];
            if ((op >> 28) > 14) return false;
            if (n + 1 == count) {
                const auto displacement = static_cast<std::int32_t>(op << 8) >> 6;
                return (op & 0x0f000000u) == 0x0a000000u
                    && pc + static_cast<std::uint32_t>(n * 4) + 8u
                        + static_cast<std::uint32_t>(displacement) == pc;
            }
            const unsigned alu = (op >> 21) & 15, rn = (op >> 16) & 15, rd = (op >> 12) & 15;
            if (word_memory && (op & 0x0c000000u) == 0x04000000u) {
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
    inline bool pure_loop_words(const std::uint32_t *words, std::size_t count, std::uint32_t pc) {
        return original_loop_words(words, count, pc, false);
    }
    inline bool word_loop_words(const std::uint32_t *words, std::size_t count, std::uint32_t pc) {
        return original_loop_words(words, count, pc, true);
    }
}
