#pragma once

#include <cstdint>

namespace eka2l1::common::code_tracking {
    // Executable-byte policy, frozen before CPU initialization. WASM trusts
    // loaded code by default; mode 0 restores mutation-compatible execution.
    // 0: exact byte validation and code-write guards; 3: trust executable bytes.
    // Mapping/lifetime invalidation remains independent.
#ifdef __EMSCRIPTEN__
    inline unsigned unsafe_code_mode = 3;
#else
    inline unsigned unsafe_code_mode = 0;
#endif
    inline bool skip_code_scans() { return unsafe_code_mode == 3; }
    inline bool skip_code_write_guards() { return unsafe_code_mode == 3; }
}
