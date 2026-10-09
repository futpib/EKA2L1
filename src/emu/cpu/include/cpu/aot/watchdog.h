#pragma once

#include <atomic>
#include <cstdint>

namespace eka2l1::arm::aot::watchdog {
    // Generated code returns progress status. The browser owns yield requests.
    alignas(64) inline std::atomic<std::uint32_t> request{0};

    inline bool requested() { return request.load(std::memory_order_relaxed) != 0; }

    template<class Writer> void emit_request(Writer &w) {
        w.i32_const(static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&request)));
        // i32.atomic.load, natural alignment, zero offset. This must not enter
        // the state-local cache: another worker changes it during execution.
        w.op(0xfe); w.b.insert(w.b.end(), {0x10, 0x02, 0x00});
    }
}
