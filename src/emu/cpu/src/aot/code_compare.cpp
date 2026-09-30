#include <cpu/aot/code_cache.h>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

namespace eka2l1::arm::aot {
    unsigned code_compare_mode = 0;

    bool equal_code_bytes(const std::uint8_t *a, const std::uint8_t *b, std::size_t size) {
#ifdef __wasm_simd128__
        if (code_compare_mode == 2) {
            // All eight loads remain in the original spans. Combine mismatch
            // bits before the branch; unequal bytes can never cancel via OR.
            while (size >= 64) {
                const auto d0 = wasm_v128_xor(wasm_v128_load(a), wasm_v128_load(b));
                const auto d1 = wasm_v128_xor(wasm_v128_load(a + 16), wasm_v128_load(b + 16));
                const auto d2 = wasm_v128_xor(wasm_v128_load(a + 32), wasm_v128_load(b + 32));
                const auto d3 = wasm_v128_xor(wasm_v128_load(a + 48), wasm_v128_load(b + 48));
                if (wasm_v128_any_true(wasm_v128_or(wasm_v128_or(d0, d1), wasm_v128_or(d2, d3)))) return false;
                a += 64; b += 64; size -= 64;
            }
        }
        if (code_compare_mode == 1 && size >= 16) {
            // Compare the last full vector instead of decomposing a short tail.
            // Both loads stay inside the original span, even for size 17. The
            // overlap repeats comparisons only; every byte is still checked.
            const auto *end_a = a + size - 16;
            const auto *end_b = b + size - 16;
            while (size > 16) {
                if (wasm_v128_any_true(wasm_v128_xor(wasm_v128_load(a), wasm_v128_load(b)))) return false;
                a += 16; b += 16; size -= 16;
            }
            return !wasm_v128_any_true(wasm_v128_xor(wasm_v128_load(end_a), wasm_v128_load(end_b)));
        }
        while (size >= 16) {
            if (wasm_v128_any_true(wasm_v128_xor(wasm_v128_load(a), wasm_v128_load(b)))) return false;
            a += 16; b += 16; size -= 16;
        }
        // At most 15 bytes remain. Constant-width memcpy loads permit unaligned
        // access without overreading and avoid a libc call for every block.
        if (size >= 8) {
            std::uint64_t x, y; std::memcpy(&x,a,8); std::memcpy(&y,b,8);
            if (x != y) return false;
            a += 8; b += 8; size -= 8;
        }
        if (size >= 4) {
            std::uint32_t x, y; std::memcpy(&x,a,4); std::memcpy(&y,b,4);
            if (x != y) return false;
            a += 4; b += 4; size -= 4;
        }
        if (size >= 2) {
            std::uint16_t x, y; std::memcpy(&x,a,2); std::memcpy(&y,b,2);
            if (x != y) return false;
            a += 2; b += 2; size -= 2;
        }
        return !size || *a == *b;
#else
        return std::memcmp(a,b,size) == 0;
#endif
    }
}
