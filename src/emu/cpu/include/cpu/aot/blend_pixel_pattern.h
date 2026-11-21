#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace eka2l1::arm::loop_js {
    struct blend_pixel_pattern {
        std::uint8_t source = 0, destination = 0, palette = 0, counter = 0, mask = 0;
        std::array<std::uint8_t, 6> work{};
        bool subtract = false;
    };
    inline bool recognize_blend_pixel(const std::uint32_t *words, std::size_t count,
            blend_pixel_pattern &result) {
        if (!words || (count != 48 && count != 42)) return false;
        blend_pixel_pattern p;
        p.subtract = count == 42;
        p.source = (words[0] >> 16) & 15; p.destination = (words[4] >> 16) & 15;
        p.palette = (words[3] >> 16) & 15; p.counter = (words[count - 2] >> 12) & 15;
        p.mask = words[1] & 15;
        p.work = {std::uint8_t((words[0] >> 12) & 15), std::uint8_t((words[6] >> 12) & 15),
            std::uint8_t((words[3] >> 12) & 15), std::uint8_t((words[8] >> 12) & 15),
            std::uint8_t((words[9] >> 12) & 15), std::uint8_t((words[4] >> 12) & 15)};
        unsigned used = 0;
        for (unsigned role : {p.source, p.destination, p.palette, p.counter, p.mask,
                p.work[0], p.work[1], p.work[2], p.work[3], p.work[4], p.work[5]}) {
            if (role > 12 || (used & (1u << role))) return false;
            used |= 1u << role;
        }
        std::size_t at = 0;
        const auto match = [&](std::uint32_t word) { return at < count && words[at++] == word; };
        const auto &w = p.work;
        for (unsigned pixel = 0; pixel < 2; ++pixel) {
            if (!match((pixel ? 0xe4d00001u : 0xe5d00000u) | p.source << 16 | w[0] << 12)
                || !match(pixel ? (0xe0000240u | p.mask << 16 | w[0] << 12 | w[0])
                    : (0xe00000a0u | w[0] << 16 | w[0] << 12 | p.mask))
                || !match(0xe1a00080u | w[0] << 12 | w[0])
                || !match(0xe19000b0u | p.palette << 16 | w[2] << 12 | w[0])
                || !match(0xe1d000b0u | p.destination << 16 | w[5] << 12)) return false;
            for (unsigned group = 0; group < 2; ++group) for (unsigned channel = 0; channel < 3; ++channel) {
                const unsigned shift = channel == 0 ? 0 : channel == 1 ? (6u << 7) | 32u : (11u << 7) | 32u;
                if (!match(0xe0000000u | p.mask << 16 | w[group * 3 + channel] << 12
                        | shift | w[group ? 5 : 2])) return false;
            }
            if (p.subtract) {
                for (unsigned channel = 0; channel < 3; ++channel)
                    if (!match(0xe0500000u | w[channel + 3] << 16 | w[channel] << 12 | w[channel])
                        || !match(0x43a00000u | w[channel] << 12)) return false;
            } else {
                for (unsigned channel = 0; channel < 3; ++channel)
                    if (!match(0xe0800000u | w[channel] << 16 | w[channel] << 12 | w[channel + 3])) return false;
                for (unsigned channel = 0; channel < 3; ++channel)
                    if (!match(0xe350001fu | w[channel] << 16)
                        || !match(0xa3a0001fu | w[channel] << 12)) return false;
            }
            if (!match(0xe1800300u | w[0] << 16 | w[1] << 12 | w[1])
                || !match(0xe1800580u | w[1] << 16 | w[1] << 12 | w[2])
                || !match(0xe0c000b2u | p.destination << 16 | w[1] << 12)) return false;
        }
        if (!match(0xe2500001u | p.counter << 16 | p.counter << 12)
            || !match(0x1a000000u | ((0u - std::uint32_t(count + 1)) & 0xffffffu)) || at != count) return false;
        result = p; return true;
    }
}
