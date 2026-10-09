#pragma once
#include <cstdint>
#include <string>
namespace eka2l1::arm::aot {
    inline bool predicated_leaves = false; // Separate original-emitter experiment.
    // Bit 128 fuses literal LDR-PC veneers, retaining the runtime load and exit.
    inline unsigned leaf_features = 0;
    // Translation size limits; compiled chains run until a required boundary.
    inline constexpr unsigned primary_window_bytes=512, leaf_instruction_limit=32,
        inline_site_limit=8;
    inline std::string execution_limits_text() {
        return std::to_string(primary_window_bytes)+","+std::to_string(leaf_instruction_limit)+","+
            std::to_string(inline_site_limit)+",0";
    }
}
