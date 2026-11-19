#if defined(EKA2L1_COUNTED_DIRECT_JS)
#include <cpu/aot/direct_js.h>
#endif
#include <cpu/aot/region_js.h>
#include <stdexcept>

namespace eka2l1::arm::region_js {
    bool enabled = false;
    static executor invoke = nullptr;
    static bool tracking = false;
    static std::uint64_t call_count = 0, instruction_count = 0;
    void reset() {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        enabled = tracking = false; invoke = nullptr;
        call_count = instruction_count = 0;
        counted_js::clear_regions();
    }
    void install(executor function, bool diagnostics) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        if (enabled || !function || !counted_js::enabled) throw std::runtime_error("Invalid region installation");
        invoke = function; tracking = diagnostics; enabled = true;
    }
    bool bind(const member *members, std::size_t count, std::uint32_t function) {
        return !enabled && counted_js::bind_region(members,count,function);
    }
    std::uint32_t invoke_admitted(ARMul_State *cpu, std::uint32_t function,
            const invocation_context &context) {
        if (!enabled || !invoke) throw std::runtime_error("Region invocation unavailable");
        return invoke(cpu,function,&context);
    }
    void record(std::uint32_t count) {
        if (tracking) { ++call_count; instruction_count += count; }
    }
    std::uint64_t calls() { return call_count; }
    std::uint64_t instructions() { return instruction_count; }
}
