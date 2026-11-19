#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace eka2l1::arm::loop_js {
    struct packed_pixel_pattern {
        std::uint8_t source = 0, destination = 0, counter = 0, value = 0, mask = 0;
        std::array<std::uint8_t, 3> channels{}, input_shift{}, output_shift{};
        std::array<std::uint8_t, 2> outputs{};
        std::uint8_t table = 255;
    };
    inline bool recognize_packed_pixel(const std::uint32_t *words, std::size_t count,
            packed_pixel_pattern &result) {
        if (!words || count != 18) return false;
        packed_pixel_pattern p;
        p.source = (words[0] >> 16) & 15; p.value = (words[0] >> 12) & 15;
        p.counter = (words[1] >> 12) & 15; p.mask = (words[2] >> 16) & 15;
        p.destination = (words[14] >> 16) & 15;
        for (unsigned n = 0; n < 3; ++n) {
            p.channels[n] = (words[2+n] >> 12) & 15;
            p.input_shift[n] = (words[2+n] >> 7) & 31;
            p.output_shift[n] = (words[5+n] >> 7) & 31;
        }
        p.outputs = {static_cast<std::uint8_t>((words[5] >> 12) & 15),
            static_cast<std::uint8_t>((words[11] >> 12) & 15)};
        const std::array<unsigned, 10> roles{p.source,p.destination,p.counter,p.value,p.mask,
            p.channels[0],p.channels[1],p.channels[2],p.outputs[0],p.outputs[1]};
        unsigned used = 0;
        for (auto role : roles) {
            if (role > 12 || (used & (1u << role))) return false;
            used |= 1u << role;
        }
        if (words[0] != (0xe4900004u | p.source << 16 | p.value << 12)
            || words[1] != (0xe2500001u | p.counter << 16 | p.counter << 12)
            || words[14] != (0xe5800000u | p.destination << 16 | p.outputs[0] << 12)
            || words[15] != (0xe5800004u | p.destination << 16 | p.outputs[1] << 12)
            || words[16] != (0xe2800008u | p.destination << 16 | p.destination << 12)
            || words[17] != 0x1affffedu) return false;
        for (unsigned half = 0; half < 2; ++half) {
            for (unsigned n = 0; n < 3; ++n) {
                // Each masked channel is wholly inside its own sixteen-bit half.
                if (p.input_shift[n] > 11) return false;
                const unsigned shift = p.input_shift[n] + half * 16;
                const auto operand = p.value | (shift ? (shift << 7) | 32u : 0u);
                if (words[2+half*6+n] != (0xe0000000u | p.mask << 16
                        | p.channels[n] << 12 | operand)) return false;
                const auto shifted = p.channels[n] | p.output_shift[n] << 7;
                const auto expected = n == 0 ? (0xe1a00000u | p.outputs[half] << 12 | shifted)
                    : (0xe0800000u | p.outputs[half] << 16 | p.outputs[half] << 12 | shifted);
                if (words[5+half*6+n] != expected) return false;
            }
        }
        result = p; return true;
    }
    inline std::uint32_t packed_pixel_profile(const packed_pixel_pattern &p) {
        std::uint32_t key = 0;
        for (unsigned n = 0; n < 3; ++n) {
            key |= std::uint32_t(p.input_shift[n]) << (n * 5);
            key |= std::uint32_t(p.output_shift[n]) << ((n + 3) * 5);
        }
        return key;
    }
}
