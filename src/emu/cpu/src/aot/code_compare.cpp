#include <cpu/aot/code_cache.h>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

namespace eka2l1::arm::aot {
    bool equal_code_bytes(const std::uint8_t *a, const std::uint8_t *b, std::size_t size) {
#ifdef __wasm_simd128__
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
