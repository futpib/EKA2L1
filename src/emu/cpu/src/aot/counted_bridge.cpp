#include <cpu/aot/counted_js.h>
#include <cpu/dyncom/armstate.h>
#include <emscripten/emscripten.h>
#if defined(EKA2L1_COUNTED_WASM)
#include <array>
#include <vector>
#endif

static void publish(ARMul_State *cpu) {
    cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
        | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_counted_register(unsigned asid, unsigned pc, const unsigned *words,
                                               unsigned count, unsigned function, unsigned shared, unsigned thumb) {
    return eka2l1::arm::counted_js::add(asid, pc, words, count, function, shared != 0, thumb != 0);
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
EM_JS(int, counted_setup, (unsigned maximum, int retain_direct), {
    try {
        const factory = Module['countedBlockFactory'];
        if (typeof factory !== 'function') return 0;
        const product = factory({env: {memory: {buffer: HEAPU8.buffer},
            tlb_read32: Module['_eka2l1_counted_read32'], tlb_write32: Module['_eka2l1_counted_write32'],
            tlb_read16: Module['_eka2l1_counted_read16'], tlb_write16: Module['_eka2l1_counted_write16'],
            tlb_read8: Module['_eka2l1_counted_read8'], tlb_write8: Module['_eka2l1_counted_write8']}});
        if (!product || !Array.isArray(product.entries) || !Array.isArray(product.functions)
            || !product.entries.length || product.entries.length > maximum
            || product.functions.some(f => typeof f !== 'function')) throw Error('Invalid counted factory');
        const directEntries = retain_direct ? [] : null;
        for (const row of product.entries) {
            if (!row || !Number.isInteger(row.pc) || row.pc < 0 || row.pc > 0xffffffff
                || !Number.isInteger(row.asid) || row.asid < 0 || row.asid > 0xffffffff
                || !Number.isInteger(row.function) || row.function < 0 || row.function >= product.functions.length
                || (row.shared !== undefined && typeof row.shared !== 'boolean')
                || (row.thumb !== undefined && typeof row.thumb !== 'boolean')
                || row.complete !== true
                || !Array.isArray(row.words) || !row.words.length || row.words.length > 128
                || row.words.some(w => !Number.isInteger(w) || w < 0 || w > (row.thumb === true ? 0xffff : 0xffffffff)))
                throw Error('Invalid counted entry');
            const pointer = Module['_malloc'](row.words.length * 4);
            if (!pointer) throw Error('Counted manifest allocation failed');
            try {
                HEAPU32.set(row.words, pointer >>> 2);
                if (!Module['_eka2l1_counted_register'](row.asid, row.pc, pointer, row.words.length, row.function, row.shared === true ? 1 : 0, row.thumb === true ? 1 : 0))
                    throw Error('Counted registration rejected');
            } finally { Module['_free'](pointer); }
            if (directEntries) directEntries.push(Object.freeze({asid:row.asid,pc:row.pc,function:row.function,
                words:row.words.length,thumb:row.thumb===true,shared:row.shared===true,index:directEntries.length,
                excluded:row.thumb!==true&&row.words.length===5&&row.words[4]===0x5afffffa,
                dispatch:row.thumb===true
                    ? ((row.words.at(-1)&0xff00)===0x4700||(row.words.at(-1)&0xf800)===0xe800
                        ||(row.words.at(-1)&0xff00)===0xbd00||(row.words.at(-1)&0xff00)===0xdf00
                        ||((row.words.at(-1)&0xfc00)===0x4400
                            &&((row.words.at(-1)&7)|((row.words.at(-1)>>>4)&8))===15))
                    : ((row.words.at(-1)>>>28)===15
                        ||(row.words.at(-1)&0x0ffffff0)===0x012fff10
                        ||(row.words.at(-1)&0x0ffffff0)===0x012fff30
                        ||((row.words.at(-1)>>>25)&7)===7
                        ||(((row.words.at(-1)>>>25)&7)===4&&(row.words.at(-1)&0x8000)!==0)
                        ||(((row.words.at(-1)>>>25)&7)<=3&&(row.words.at(-1)&0xf000)===0xf000))}));
        }
        Module['__countedFunctions'] = Object.freeze([...product.functions]);
        Module['__countedNativeWasm'] = product.abi === 'offline-counted-wasm-v1';
        if (directEntries) Module['__countedDirectEntries'] = Object.freeze(directEntries);
        else delete Module['__countedDirectEntries'];
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
#if defined(EKA2L1_COUNTED_WASM)
using wasm_function = unsigned (*)(ARMul_State *);
static std::vector<wasm_function> wasm_functions;
EM_JS(void, counted_wasm_reset, (), {
    const slots = Module['__countedWasmSlots'];
    if (slots) {
        const table = wasmExports['__indirect_function_table'];
        for (let i = 0; i < slots.used; ++i) WebAssembly.Table.prototype.set.call(table, slots.base + i, null);
        slots.used = 0;
    }
});
EM_JS(int, counted_wasm_setup, (unsigned *indices, unsigned maximum), {
    try {
        if (!Module['__countedNativeWasm']) return 0;
        const functions = Module['__countedFunctions'];
        if (!Array.isArray(functions) || !functions.length || functions.length > maximum)
            throw Error('Invalid native counted function count');
        const table = wasmExports['__indirect_function_table'];
        if (!(table instanceof WebAssembly.Table)) throw Error('Native counted table unavailable');
        let slots = Module['__countedWasmSlots'];
        if (!slots) {
            slots = {base: WebAssembly.Table.prototype.grow.call(table, maximum), capacity: maximum, used: 0};
            Module['__countedWasmSlots'] = slots;
        }
        if (slots.capacity !== maximum || slots.used) throw Error('Native counted slots are not idle');
        // Import validation checks the real Wasm signature without executing it.
        const signature = Module['__countedWasmSignature'] || (Module['__countedWasmSignature'] =
            new WebAssembly.Module(new Uint8Array([0,97,115,109,1,0,0,0,
                1,6,1,96,1,127,1,127,2,9,1,3,101,110,118,1,102,0,0])));
        slots.used = functions.length;
        for (let i = 0; i < functions.length; ++i) {
            new WebAssembly.Instance(signature, {env: {f: functions[i]}});
            WebAssembly.Table.prototype.set.call(table, slots.base + i, functions[i]);
            HEAPU32[(indices >>> 2) + i] = slots.base + i;
        }
        return functions.length;
    } catch (error) { Module['__countedError'] = String(error); return -1; }
});
EM_JS(void, counted_wasm_error, (), {
    Module['__countedError'] = 'Native counted invocation failed';
});
static unsigned counted_wasm_call(ARMul_State *cpu, unsigned index) {
    try {
        if (index < wasm_functions.size()) {
            const unsigned result = wasm_functions[index](cpu);
            if (result <= 128) return result;
        }
    } catch (...) {}
    counted_wasm_error();
    return 0xffffffffu;
}
#endif
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_counted_configure(int diagnostics) {
    eka2l1::arm::counted_js::reset();
#if defined(EKA2L1_COUNTED_WASM)
    wasm_functions.clear();
    counted_wasm_reset();
#endif
    const int entries = counted_setup(eka2l1::arm::counted_js::entry_limit,
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        1
#else
        0
#endif
    );
    if (entries > 0) {
        auto invoke = counted_call;
#if defined(EKA2L1_COUNTED_WASM)
        std::array<unsigned, eka2l1::arm::counted_js::entry_limit> indices{};
        const int functions = counted_wasm_setup(indices.data(), indices.size());
        if (functions < 0) {
            counted_wasm_reset();
            eka2l1::arm::counted_js::reset();
            return -1;
        }
        for (int i = 0; i < functions; ++i)
            wasm_functions.push_back(reinterpret_cast<wasm_function>(indices[i]));
        if (functions) invoke = counted_wasm_call;
#endif
        eka2l1::arm::counted_js::install(invoke, diagnostics != 0);
    }
    else eka2l1::arm::counted_js::reset();
    return entries;
}
EMSCRIPTEN_KEEPALIVE void eka2l1_counted_disable() {
    eka2l1::arm::counted_js::reset();
#if defined(EKA2L1_COUNTED_WASM)
    wasm_functions.clear();
    counted_wasm_reset();
#endif
}
#if defined(EKA2L1_COUNTED_WASM)
EMSCRIPTEN_KEEPALIVE int eka2l1_counted_wasm_active() { return !wasm_functions.empty(); }
#endif
EMSCRIPTEN_KEEPALIVE double eka2l1_counted_calls() { return eka2l1::arm::counted_js::calls(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_counted_instructions() { return eka2l1::arm::counted_js::instructions(); }
EMSCRIPTEN_KEEPALIVE int eka2l1_counted_diagnostics() { return eka2l1::arm::counted_js::diagnostics(); }
}
