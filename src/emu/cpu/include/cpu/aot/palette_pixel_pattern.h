#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace eka2l1::arm::loop_js {
    struct palette_pixel_pattern {
        std::uint8_t source = 0, destination = 0, palette = 0, counter = 0, value = 0, mask = 0;
        std::array<std::uint8_t, 6> work{};
        std::uint32_t stride = 0;
        bool shifted = false;
    };
    inline bool recognize_palette_pixel(const std::uint32_t *words, std::size_t count,
            palette_pixel_pattern &result) {
        if (!words || (count != 25 && count != 26)) return false;
        palette_pixel_pattern p;
        p.shifted = count == 26;
        p.source = (words[0] >> 16) & 15; p.value = (words[0] >> 12) & 15;
        p.counter = (words[1] >> 12) & 15; p.mask = (words[2] >> 16) & 15;
        p.destination = (words[20] >> 12) & 15;
        p.palette = (words[p.shifted ? 8 : 6] >> 16) & 15;
        for (unsigned n = 0; n < 4; ++n) p.work[n] = (words[2+n] >> 12) & 15;
        p.work[4] = (words[p.shifted ? 6 : 14] >> 12) & 15;
        p.work[5] = (words[p.shifted ? 7 : 15] >> 12) & 15;
        const std::array<unsigned, 12> roles{p.source,p.destination,p.palette,p.counter,p.value,p.mask,
            p.work[0],p.work[1],p.work[2],p.work[3],p.work[4],p.work[5]};
        unsigned used = 0;
        for (auto role : roles) {
            if (role > 12 || (used & (1u << role))) return false;
            used |= 1u << role;
        }
        // STM writes registers in ascending architectural order.
        if ((!p.shifted && p.work[0] >= p.work[1])
            || p.work[1] >= p.work[2] || p.work[2] >= p.work[3]) return false;
        const auto immediate = words[20] & 255u, rotate = ((words[20] >> 8) & 15u) * 2;
        p.stride = rotate ? (immediate >> rotate) | (immediate << (32 - rotate)) : immediate;
        if (p.stride < 16 || p.stride > 4096 || (p.stride & 3u)
            || words[0] != (0xe4900004u | p.source << 16 | p.value << 12)
            || words[1] != (0xe2500001u | p.counter << 16 | p.counter << 12)
            || words[20] != (0xe2800000u | p.destination << 16 | p.destination << 12 | (words[20] & 4095u))
            || words[count-1] != (0x1a000000u | ((0u - std::uint32_t(count + 1)) & 0xffffffu))) return false;
        const auto index = [&](unsigned at, unsigned nibble, unsigned destination) {
            const unsigned shift = nibble ? ((nibble * 4 - 1) << 7) | 64u : 128u;
            return words[at] == (0xe0000000u | p.mask << 16 | destination << 12 | shift | p.value);
        };
        const auto half = [&](unsigned at, unsigned destination) {
            return words[at] == (0xe19000b0u | p.palette << 16 | destination << 12 | destination);
        };
        const auto pack = [&](unsigned at, unsigned destination, unsigned low, unsigned high) {
            return words[at] == (0xe1800800u | low << 16 | destination << 12 | high);
        };
        unsigned output_mask = 0;
        if (!p.shifted) {
            for (unsigned n = 0; n < 4; ++n) {
                if (!index(2+n,n,p.work[n]) || !half(6+n,p.work[n])
                    || !index(12+n,4+n,p.work[2+n]) || !half(16+n,p.work[2+n])) return false;
                output_mask |= 1u << p.work[n];
            }
            if (!pack(10,p.work[0],p.work[0],p.work[1]) || !pack(11,p.work[1],p.work[2],p.work[3])
                || !pack(21,p.work[2],p.work[2],p.work[3]) || !pack(22,p.work[3],p.work[4],p.work[5])
                || words[23] != (0xe8800000u | p.destination << 16 | output_mask)) return false;
        } else {
            for (unsigned n = 0; n < 6; ++n)
                if (!index(2+n,n,p.work[n]) || !half(8+n,p.work[n])) return false;
            for (unsigned n = 1; n < 4; ++n) output_mask |= 1u << p.work[n];
            if (!pack(14,p.work[1],p.work[1],p.work[2]) || !pack(15,p.work[2],p.work[3],p.work[4])
                || !index(16,6,p.work[3]) || !index(17,7,p.work[4])
                || !half(18,p.work[3]) || !half(19,p.work[4])
                || !pack(21,p.work[3],p.work[5],p.work[3])
                || words[22] != (0xe14000b2u | p.destination << 16 | p.work[0] << 12)
                || words[23] != (0xe8800000u | p.destination << 16 | output_mask)
                || words[24] != (0xe1c000bcu | p.destination << 16 | p.work[4] << 12)) return false;
        }
        result = p; return true;
    }
}
