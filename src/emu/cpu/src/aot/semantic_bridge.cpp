#include <cpu/aot/semantic_js.h>
#include <cpu/aot/counted_js.h>
#include <cpu/arm_interface.h>
#include <cpu/aot/loop_js.h>
#include <cpu/dyncom/armstate.h>
#include <emscripten/emscripten.h>
#include <atomic>
#include <cstddef>

static_assert(sizeof(eka2l1::arm::semantic_js::source_span) == 12);
extern "C" {
EMSCRIPTEN_KEEPALIVE unsigned eka2l1_semantic_resolve(ARMul_State *cpu, unsigned address, unsigned bytes, int write) {
    return static_cast<unsigned>(eka2l1::arm::semantic_js::resolve(cpu, address, bytes, write != 0));
}
}
EM_JS(unsigned, semantic_call, (ARMul_State *cpu, unsigned function_index, unsigned budget,
        unsigned generation, unsigned asid, unsigned mapping_low, unsigned mapping_high), {
    try {
        var result = Module['__semanticFunctions'][function_index](cpu, budget, generation, asid, mapping_low, mapping_high);
        if (!Number.isInteger(result) || result < 0 || result > budget) throw Error('Invalid original semantic count');
        return result >>> 0;
    } catch (error) { Module['__semanticError'] = String(error); return 0xffffffff; }
});
static unsigned execute_semantic(ARMul_State *cpu, unsigned function, unsigned budget) {
    const auto mapping = cpu->parent()->code_mapping_generation->load(std::memory_order_acquire);
    return semantic_call(cpu, function, budget, cpu->trans_gen, cpu->instruction_cache_asid,
        static_cast<unsigned>(mapping), static_cast<unsigned>(mapping >> 32));
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_semantic_register(unsigned asid, unsigned pc, const unsigned *words, unsigned count,
        unsigned function, const eka2l1::arm::semantic_js::source_span *sources, unsigned source_count, int diagnostics, int original_fallback) {
    if (!eka2l1::arm::loop_js::enabled) return 0;
    if (!eka2l1::arm::semantic_js::install(asid, pc, function, sources, source_count, execute_semantic, diagnostics != 0, original_fallback != 0)) return 0;
    return eka2l1::arm::counted_js::bind_semantic(asid, pc, words, count, function);
}
}
EM_JS(int, semantic_setup, (const unsigned *layout, unsigned count, int diagnostics, unsigned shifter), {
    try {
        const factory = Module['semanticFactory'];
        if (typeof factory !== 'function') return 0;
        if (count !== 8 || layout & 3 || layout + count * 4 > HEAPU8.length) throw Error('Invalid semantic CPU layout');
        const geometry = Array.from(HEAPU32.subarray(layout >>> 2, (layout >>> 2) + count));
        const product = factory({geometry, shifterCarry: shifter, diagnostics: diagnostics !== 0, memory: {buffer: HEAPU8.buffer},
            heap: () => HEAPU8, resolve: Module['_eka2l1_semantic_resolve']});
        if (!product || product.abi !== 'original-resource-symbol-v1' || !Array.isArray(product.entries) || !product.entries.length || product.entries.length > 8)
            throw Error('Invalid semantic factory');
        const functions = [], allocations = [];
        const integer = value => Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
        const put = words => {
            if (!Array.isArray(words) || !words.length || words.length > 16384 || words.some(w => !integer(w))) throw Error('Invalid semantic original words');
            const pointer = Module['_malloc'](words.length * 4);
            if (!pointer) throw Error('Semantic source allocation');
            allocations.push(pointer); HEAPU32.set(words, pointer >>> 2); return pointer;
        };
        try {
            for (const [index, row] of product.entries.entries()) {
                if (!row || !integer(row.asid) || !integer(row.pc) || row.pc & 3 || typeof row.execute !== 'function'
                    || (row.originalFallback !== undefined && typeof row.originalFallback !== 'boolean')
                    || !Array.isArray(row.sources) || !row.sources.length || row.sources.length > 64) throw Error('Invalid semantic entry');
                const words = put(row.words), spans = [];
                for (const source of row.sources) {
                    if (!source || !integer(source.pc) || source.pc & 3 || !Array.isArray(source.words) || source.words.length > 4096)
                        throw Error('Invalid semantic source span');
                    spans.push(source.pc, put(source.words), source.words.length);
                }
                const sources = put(spans);
                if (!Module['_eka2l1_semantic_register'](row.asid, row.pc, words, row.words.length, index, sources, row.sources.length, diagnostics, row.originalFallback === true ? 1 : 0))
                    throw Error('Original semantic registration refused');
                if (diagnostics) functions.push((...args) => {
                    const result = row.execute(...args);
                    if (result) { Module['__semanticCalls']++; Module['__semanticInstructions'] += result; }
                    return result;
                });
                else functions.push(row.execute);
            }
            Module['__semanticCalls'] = diagnostics ? 0 : null;
            Module['__semanticInstructions'] = diagnostics ? 0 : null;
            Module['__semanticFunctions'] = Object.freeze(functions);
            Module['__semanticBindings'] = diagnostics ? product.stats : null;
            Module['__semanticError'] = null;
            return functions.length;
        } finally { for (const pointer of allocations) Module['_free'](pointer); }
    } catch (error) { delete Module['__semanticFunctions']; Module['__semanticBindings'] = null; Module['__semanticCalls'] = Module['__semanticInstructions'] = null; Module['__semanticError'] = String(error); return -1; }
});
extern "C" {
EMSCRIPTEN_KEEPALIVE double eka2l1_semantic_authentications() { return eka2l1::arm::semantic_js::authentications(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_semantic_resolutions() { return eka2l1::arm::semantic_js::resolutions(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_semantic_resolved_pages() { return eka2l1::arm::semantic_js::resolved_pages(); }
EMSCRIPTEN_KEEPALIVE int eka2l1_semantic_configure(int diagnostics) {
    eka2l1::arm::semantic_js::reset();
    eka2l1::arm::counted_js::clear_semantics();
    const unsigned layout[]{sizeof(ARMul_State), offsetof(ARMul_State, Reg), offsetof(ARMul_State, Cpsr),
        offsetof(ARMul_State, NFlag), offsetof(ARMul_State, ZFlag), offsetof(ARMul_State, CFlag),
        offsetof(ARMul_State, VFlag), offsetof(ARMul_State, TFlag)};
    const auto result = semantic_setup(layout, 8, diagnostics, offsetof(ARMul_State, shifter_carry_out));
    if (result <= 0) { eka2l1::arm::semantic_js::reset(); eka2l1::arm::counted_js::clear_semantics(); }
    return result;
}
}
