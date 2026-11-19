#include <cpu/aot/loop_js.h>
#include <cpu/aot/counted_js.h>
#include <cpu/dyncom/armstate.h>
#include <emscripten/emscripten.h>

static_assert(sizeof(eka2l1::arm::loop_js::invocation_context) == 7 * sizeof(std::uint32_t));

extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_loop_register(unsigned asid, unsigned pc, const unsigned *words,
        unsigned count, unsigned function_index, unsigned shared) {
    if (shared) return 0;
    return eka2l1::arm::counted_js::bind_loop(asid, pc, words, count, function_index);
}
EMSCRIPTEN_KEEPALIVE int eka2l1_loop_register_v2(unsigned asid, unsigned pc, const unsigned *words,
        unsigned count, unsigned function_index, unsigned shared) {
    if (shared) return 0;
    return eka2l1::arm::counted_js::bind_word_loop(asid, pc, words, count, function_index);
}
}
EM_JS(int, loop_setup, (unsigned maximum), {
    try {
        const factory = Module['loopBlockFactory'];
        if (typeof factory !== 'function') return 0;
        const contextField = (cpu, field) => {
            if (Module['__loopActiveCPU'] !== cpu || !Number.isInteger(field) || field < 0 || field > 6)
                throw Error('Loop context queried outside invocation');
            return HEAPU32[(Module['__loopActiveContext'] >>> 2) + field];
        };
        const product = factory({env: {memory: {buffer: HEAPU8.buffer},
            tlb_read32: Module['_eka2l1_counted_read32'], tlb_write32: Module['_eka2l1_counted_write32'],
            tlb_read8: Module['_eka2l1_counted_read8'], tlb_write8: Module['_eka2l1_counted_write8'],
            tlb_read16: Module['_eka2l1_counted_read16'], tlb_write16: Module['_eka2l1_counted_write16'],
            loop_budget: cpu => contextField(cpu, 0), loop_context: contextField}});
        if (!product || !['counted-pure-arm-loop-v1','counted-original-arm-loop-v2'].includes(product.abi)
            || !Array.isArray(product.entries) || !Array.isArray(product.functions)
            || !product.entries.length || product.entries.length > maximum
            || !product.functions.length || product.functions.length > maximum
            || product.functions.some(f => typeof f !== 'function')) throw Error('Invalid loop factory');
        const wordMemory = product.abi === 'counted-original-arm-loop-v2';
        if (wordMemory && ['read32','write32','read8','write8','read16','write16']
            .some(name => typeof Module['_eka2l1_counted_' + name] !== 'function'))
            throw Error('Missing actual counted memory helper');
        const register = Module[wordMemory ? '_eka2l1_loop_register_v2' : '_eka2l1_loop_register'];
        for (const row of product.entries) {
            if (!row || !Number.isInteger(row.pc) || row.pc < 0 || row.pc > 0xffffffff
                || !Number.isInteger(row.asid) || row.asid < 0 || row.asid > 0xffffffff
                || !Number.isInteger(row.function) || row.function < 0 || row.function >= product.functions.length
                || (row.shared !== undefined && row.shared !== false)
                || (row.thumb !== undefined && row.thumb !== false) || row.complete !== true
                || !Array.isArray(row.words) || row.words.length < 2 || row.words.length > 128
                || row.words.some(w => !Number.isInteger(w) || w < 0 || w > 0xffffffff))
                throw Error('Invalid loop entry');
            const pointer = Module['_malloc'](row.words.length * 4);
            if (!pointer) throw Error('Loop manifest allocation failed');
            try {
                HEAPU32.set(row.words, pointer >>> 2);
                if (!register(row.asid, row.pc, pointer, row.words.length,
                        row.function, row.shared === true ? 1 : 0)) throw Error('Loop registration rejected');
            } finally { Module['_free'](pointer); }
        }
        Module['__loopFunctions'] = Object.freeze([...product.functions]);
        Module['__loopABI'] = product.abi;
        Module['__loopError'] = null;
        return product.entries.length;
    } catch (error) { Module['__loopError'] = String(error); return -1; }
});
EM_JS(unsigned, loop_call, (ARMul_State *cpu, unsigned function_index, const void *context), {
    try {
        if (Module['__loopActiveCPU'] !== undefined) throw Error('Nested loop invocation');
        if (!Number.isInteger(context) || context <= 0 || (context & 3) || context > HEAPU8.length - 28)
            throw Error('Invalid loop invocation context');
        const remaining = HEAPU32[context >>> 2];
        if (!remaining || remaining > 65535) throw Error('Invalid loop invocation budget');
        Module['__loopActiveCPU'] = cpu;
        Module['__loopActiveContext'] = context;
        const result = Module['__loopFunctions'][function_index](cpu);
        const wordMemory = Module['__loopABI'] === 'counted-original-arm-loop-v2';
        const count = wordMemory ? result & 65535 : result;
        if (!Number.isInteger(result) || result <= 0 || result > (wordMemory ? 131071 : remaining)
            || count <= 0 || count > remaining) throw Error('Invalid loop result');
        return result >>> 0;
    } catch (error) { Module['__loopError'] = String(error); return 0xffffffff; }
    finally { delete Module['__loopActiveCPU']; delete Module['__loopActiveContext']; }
});
static std::uint32_t invoke_loop(ARMul_State *cpu, std::uint32_t function_index,
        const eka2l1::arm::loop_js::invocation_context *context) {
    return loop_call(cpu, function_index, context);
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_loop_configure(int diagnostics) {
    eka2l1::arm::loop_js::reset();
    if ((diagnostics != 0 && diagnostics != 1) || !eka2l1::arm::counted_js::enabled) return -1;
    const int entries = loop_setup(eka2l1::arm::counted_js::entry_limit);
    if (entries > 0) eka2l1::arm::loop_js::install(invoke_loop, diagnostics != 0);
    else eka2l1::arm::loop_js::reset();
    return entries;
}
EMSCRIPTEN_KEEPALIVE void eka2l1_loop_disable() { eka2l1::arm::loop_js::reset(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_loop_calls() { return eka2l1::arm::loop_js::calls(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_loop_instructions() { return eka2l1::arm::loop_js::instructions(); }
#if defined(EKA2L1_PACKED_PIXEL_JS)
EMSCRIPTEN_KEEPALIVE int eka2l1_loop_packed_pixel_configure(int enabled) {
    if (enabled != 0 && enabled != 1) return -1;
    return eka2l1::arm::loop_js::configure_packed_pixel(enabled != 0);
}
EMSCRIPTEN_KEEPALIVE double eka2l1_loop_packed_pixel_calls() { return eka2l1::arm::loop_js::packed_pixel_calls(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_loop_packed_pixel_instructions() { return eka2l1::arm::loop_js::packed_pixel_iterations() * 18; }
EMSCRIPTEN_KEEPALIVE double eka2l1_loop_packed_pixel_declines() { return eka2l1::arm::loop_js::packed_pixel_declines(); }
#endif
}
