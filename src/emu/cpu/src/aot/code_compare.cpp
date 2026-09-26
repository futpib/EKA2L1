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
#endif
        return std::memcmp(a,b,size) == 0;
    }
}
