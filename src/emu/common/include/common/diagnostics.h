#pragma once

#include <common/diagnostics_config.h>

namespace eka2l1::common::diagnostics {
#if !defined(__EMSCRIPTEN__) || defined(EKA2L1_WASM_DIAGNOSTICS)
    inline constexpr bool available = true;
    using flag = bool;
#else
    inline constexpr bool available = false;
    // Preserve configuration call sites while making every diagnostic branch
    // a compile-time false in production WASM. Public APIs reject attempts to
    // enable unavailable diagnostics; native and diagnostic builds keep bools.
    struct flag {
        constexpr flag(bool = false) {}
        constexpr operator bool() const { return false; }
        constexpr flag &operator=(bool) { return *this; }
    };
#endif
}
