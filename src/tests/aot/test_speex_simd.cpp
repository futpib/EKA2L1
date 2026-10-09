#include <drivers/audio/speex_wasm_simd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
    std::uint32_t random = 98273;
    auto sample = [&] {
        random = random * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(random)) / 1048576.0f;
    };
    unsigned checks = 0;
    for (unsigned length : {0u, 1u, 2u, 3u, 7u, 16u, 63u, 128u, 257u, 513u})
    for (unsigned step : {1u, 2u, 4u, 8u, 16u, 32u})
    for (unsigned offset = 0; offset < 4; ++offset)
    for (unsigned trial = 0; trial < 32; ++trial) {
        std::vector<float> input(length), coefficients(length * step + 8);
        std::array<float, 4> fraction, sum{};
        for (auto &x : input) x = sample();
        for (auto &x : coefficients) x = sample();
        for (auto &x : fraction) x = sample();
        for (unsigned i = 0; i < length; ++i)
            for (unsigned lane = 0; lane < 4; ++lane)
                sum[lane] += input[i] * coefficients[offset + i * step + lane];
        const float expected = ((fraction[0] * sum[0] + fraction[1] * sum[1])
            + fraction[2] * sum[2]) + fraction[3] * sum[3];
        const float actual = interpolate_product_single(input.data(), coefficients.data() + offset,
            length, step, fraction.data());
        if (std::memcmp(&actual, &expected, sizeof(float))) {
            std::cerr << "FAIL SIMD interpolation " << length << ',' << step << ',' << offset << '\n';
            return 1;
        }
        ++checks;
    }
    std::cout << "PASS " << checks << " exact scalar/SIMD interpolation comparisons\n";
}
