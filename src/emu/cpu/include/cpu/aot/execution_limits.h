#pragma once
#include <cstdint>
#include <string>
namespace eka2l1::arm::aot {
    inline bool predicated_leaves = false; // Separate original-emitter experiment.
    // Bit 128 fuses literal LDR-PC veneers, retaining the runtime load and exit.
    inline unsigned leaf_features = 0;
    // Adopted translation/runner limits. These do not
    // change the owning CPU run budget or guest scheduling quantum.
    inline constexpr unsigned primary_window_bytes=512, leaf_instruction_limit=32,
        inline_site_limit=8, runner_region_limit=512;
    inline std::string execution_limits_text() {
        return std::to_string(primary_window_bytes)+","+std::to_string(leaf_instruction_limit)+","+
            std::to_string(inline_site_limit)+","+std::to_string(runner_region_limit);
    }
}
