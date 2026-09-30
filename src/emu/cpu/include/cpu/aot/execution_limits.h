#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
namespace eka2l1::arm::aot {
    // Translation/runner limits, fixed before guest execution. These do not
    // change the owning CPU run budget or guest scheduling quantum.
    inline unsigned primary_window_bytes=512, leaf_instruction_limit=16,
        inline_site_limit=8, runner_region_limit=512;
    inline bool configure_execution_limits(unsigned window,unsigned leaf,unsigned sites,unsigned runner) {
        if(window<128 || window>2048 || (window&3) || !leaf || leaf>64 || sites>16 || runner>4096)return false;
        primary_window_bytes=window;leaf_instruction_limit=leaf;
        inline_site_limit=sites;runner_region_limit=runner;return true;
    }
    inline bool parse_execution_limits(const char *text) {
        unsigned w,l,s,r;int end=0;
        if(!text || std::sscanf(text,"%u,%u,%u,%u%n",&w,&l,&s,&r,&end)!=4 || text[end])return false;
        // Enforce a canonical decimal marker, rejecting signs/whitespace.
        const auto canonical=std::to_string(w)+","+std::to_string(l)+","+std::to_string(s)+","+std::to_string(r);
        return canonical==text && configure_execution_limits(w,l,s,r);
    }
    inline std::string execution_limits_text() {
        return std::to_string(primary_window_bytes)+","+std::to_string(leaf_instruction_limit)+","+
            std::to_string(inline_site_limit)+","+std::to_string(runner_region_limit);
    }
}
