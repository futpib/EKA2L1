#include <cpu/aot/counted_js.h>
#include <cpu/dyncom/armstate.h>
#include <emscripten/emscripten.h>

static void publish(ARMul_State *cpu) {
    cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
        | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_counted_register(unsigned asid, unsigned pc, const unsigned *words,
                                               unsigned count, unsigned function) {
    return eka2l1::arm::counted_js::add(asid, pc, words, count, function);
}
#define READ(bits) \
EMSCRIPTEN_KEEPALIVE unsigned eka2l1_counted_read##bits(ARMul_State *cpu, unsigned address) { \
    publish(cpu); return cpu->ReadMemory##bits(address); }
#define WRITE(bits, type) \
EMSCRIPTEN_KEEPALIVE void eka2l1_counted_write##bits(ARMul_State *cpu, unsigned address, unsigned value) { \
    publish(cpu); cpu->WriteMemory##bits(address, static_cast<type>(value)); }
READ(8) READ(16) READ(32)
WRITE(8, std::uint8_t) WRITE(16, std::uint16_t) WRITE(32, std::uint32_t)
#undef READ
#undef WRITE
}
EM_JS(int, counted_setup, (), {
    try {
        const factory = Module['countedBlockFactory'];
        if (typeof factory !== 'function') return 0;
        const product = factory({env: {memory: {buffer: HEAPU8.buffer},
            tlb_read32: Module['_eka2l1_counted_read32'], tlb_write32: Module['_eka2l1_counted_write32'],
            tlb_read16: Module['_eka2l1_counted_read16'], tlb_write16: Module['_eka2l1_counted_write16'],
            tlb_read8: Module['_eka2l1_counted_read8'], tlb_write8: Module['_eka2l1_counted_write8']}});
        if (!product || !Array.isArray(product.entries) || !Array.isArray(product.functions)
            || !product.entries.length || product.entries.length > 4096
            || product.functions.some(f => typeof f !== 'function')) throw Error('Invalid counted factory');
        for (const row of product.entries) {
            if (!row || !Number.isInteger(row.pc) || row.pc < 0 || row.pc > 0xffffffff
                || !Number.isInteger(row.asid) || row.asid < 0 || row.asid > 0xffffffff
                || !Number.isInteger(row.function) || row.function < 0 || row.function >= product.functions.length
                || row.complete !== true
                || !Array.isArray(row.words) || !row.words.length || row.words.length > 128
                || row.words.some(w => !Number.isInteger(w) || w < 0 || w > 0xffffffff))
                throw Error('Invalid counted entry');
            const pointer = Module['_malloc'](row.words.length * 4);
            if (!pointer) throw Error('Counted manifest allocation failed');
            try {
                HEAPU32.set(row.words, pointer >>> 2);
                if (!Module['_eka2l1_counted_register'](row.asid, row.pc, pointer, row.words.length, row.function))
                    throw Error('Counted registration rejected');
            } finally { Module['_free'](pointer); }
        }
        Module['__countedFunctions'] = Object.freeze([...product.functions]);
        Module['__countedError'] = null;
        return product.entries.length;
    } catch (error) { Module['__countedError'] = String(error); return -1; }
});
EM_JS(unsigned, counted_call, (ARMul_State *cpu, unsigned function_index), {
    try {
        const result = Module['__countedFunctions'][function_index](cpu);
        if (!Number.isInteger(result) || result < 0 || result > 128) throw Error('Invalid counted result');
        return result >>> 0;
    } catch (error) { Module['__countedError'] = String(error); return 0xffffffff; }
});
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_counted_configure(int diagnostics) {
    eka2l1::arm::counted_js::reset();
    const int entries = counted_setup();
    if (entries > 0) eka2l1::arm::counted_js::install(counted_call, diagnostics != 0);
    else eka2l1::arm::counted_js::reset();
    return entries;
}
EMSCRIPTEN_KEEPALIVE void eka2l1_counted_disable() { eka2l1::arm::counted_js::reset(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_counted_calls() { return eka2l1::arm::counted_js::calls(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_counted_instructions() { return eka2l1::arm::counted_js::instructions(); }
EMSCRIPTEN_KEEPALIVE int eka2l1_counted_diagnostics() { return eka2l1::arm::counted_js::diagnostics(); }
}
