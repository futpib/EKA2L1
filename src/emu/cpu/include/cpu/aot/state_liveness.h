#pragma once

#include <cpu/aot/wasm_emitter.h>
#include <cstddef>

namespace eka2l1::arm::aot {
    // Exact, stack-neutral transfers inserted by state_local_cache::finish.
    struct state_transfer {
        std::size_t begin, end;
        std::uint32_t slot;
        bool reload;
    };

    // Only deletes transfers. Unknown bytecode or unsupported cache layouts
    // retain the original body and private-call operand offsets.
    bool prune_state_transfers(wasm_func_def &function,
        const std::vector<state_transfer> &transfers,
        std::uint32_t first_local, std::uint32_t local_count);
}
