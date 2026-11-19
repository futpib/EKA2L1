#include <cpu/aot/direct_js.h>
#include <cpu/aot/counted_js.h>
#include <stdexcept>

namespace eka2l1::arm::direct_js {
    bool enabled = false;
    static executor execute = nullptr;
    static bool active = false, tracking = false;
    static std::uint32_t epoch = 1;
    static std::uint64_t call_count = 0, instruction_count = 0, block_count = 0;
    void require_idle() {
        if (active) throw std::runtime_error("Lifetime change during direct execution");
    }
    void invalidate() {
        require_idle();
        ++epoch;
    }
    void reset() {
        invalidate();
        enabled = tracking = false; execute = nullptr;
        call_count = instruction_count = block_count = 0;
    }
    void install(executor function, bool diagnostics) {
        require_idle();
        if (enabled || !function || !counted_js::enabled)
            throw std::runtime_error("Invalid direct executor installation");
        execute = function; tracking = diagnostics; enabled = true;
    }
    std::uint32_t *epoch_address() { return &epoch; }
    std::uint32_t invoke(ARMul_State *cpu, invocation_context *context) {
        require_idle();
        if (!enabled || !execute) return 0;
        struct invocation_scope {
            invocation_scope() { active = true; }
            ~invocation_scope() { active = false; }
        } scope;
        if (tracking) ++call_count;
        return execute(cpu, context);
    }
    void record(std::uint32_t count, std::uint32_t executed_blocks) {
        if (tracking) { instruction_count += count; block_count += executed_blocks; }
    }
    std::uint64_t calls() { return call_count; }
    std::uint64_t instructions() { return instruction_count; }
    std::uint64_t blocks() { return block_count; }
}
