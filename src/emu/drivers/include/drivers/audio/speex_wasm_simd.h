#pragma once

#include <wasm_simd128.h>

// Each lane follows one original accumulator, in the same sample order.
// Keep the final scalar addition order so the shared PCM path stays exact.
#define OVERRIDE_INTERPOLATE_PRODUCT_SINGLE
static inline float interpolate_product_single(const float *input, const float *coefficients,
    unsigned length, unsigned oversample, float *fraction) {
    v128_t sum = wasm_f32x4_splat(0.0f);
    for (unsigned i = 0; i < length; ++i) {
        sum = wasm_f32x4_add(sum, wasm_f32x4_mul(wasm_f32x4_splat(input[i]),
            wasm_v128_load(coefficients)));
        coefficients += oversample;
    }
    const v128_t product = wasm_f32x4_mul(wasm_v128_load(fraction), sum);
    return ((wasm_f32x4_extract_lane(product, 0) + wasm_f32x4_extract_lane(product, 1))
        + wasm_f32x4_extract_lane(product, 2)) + wasm_f32x4_extract_lane(product, 3);
}
