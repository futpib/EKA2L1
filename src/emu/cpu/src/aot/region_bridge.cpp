#include <cpu/aot/region_js.h>
#include <cpu/aot/counted_js.h>
#include <emscripten/emscripten.h>

static_assert(sizeof(eka2l1::arm::region_js::invocation_context) == 47 * sizeof(std::uint32_t));

extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_region_register(const unsigned *records, unsigned count,
        unsigned function_index) {
    using namespace eka2l1::arm;
    if (!records || count < 2 || count > region_js::member_limit
            || function_index >= counted_js::entry_limit) return 0;
    std::array<region_js::member, region_js::member_limit> members{};
    for (unsigned index = 0; index < count; ++index) {
        const auto *row = records + index * 4;
        members[index] = {row[0], row[1], reinterpret_cast<const std::uint32_t *>(
            static_cast<std::uintptr_t>(row[2])), row[3]};
    }
    return region_js::bind(members.data(), count, function_index);
}
}
EM_JS(void, region_clear, (int clear_error), {
    if (clear_error) Module['__regionError'] = null;
    delete Module['__regionFunctions'];
    delete Module['__regionMemberCounts'];
    delete Module['__regionActiveCPU'];
    delete Module['__regionActiveContext'];
});
EM_JS(int, region_setup, (unsigned maximum), {
    try {
        const factory = Module['regionBlockFactory'];
        if (typeof factory !== 'function') return 0;
        if (['read32','write32','read8','write8','read16','write16']
            .some(name => typeof Module['_eka2l1_counted_' + name] !== 'function')
            || typeof Module['_eka2l1_region_register'] !== 'function'
            || typeof Module['_malloc'] !== 'function' || typeof Module['_free'] !== 'function')
            throw Error('Missing actual counted region helper');
        const contextPointer = cpu => {
            if (Module['__regionActiveCPU'] !== cpu || Module['__regionActiveContext'] === undefined)
                throw Error('Region context queried outside invocation');
            return Module['__regionActiveContext'];
        };
        const product = factory({env: {memory: {buffer: HEAPU8.buffer},
            tlb_read32: Module['_eka2l1_counted_read32'], tlb_write32: Module['_eka2l1_counted_write32'],
            tlb_read8: Module['_eka2l1_counted_read8'], tlb_write8: Module['_eka2l1_counted_write8'],
            tlb_read16: Module['_eka2l1_counted_read16'], tlb_write16: Module['_eka2l1_counted_write16'],
            region_context: contextPointer}});
        if (!product || product.abi !== 'counted-original-arm-region-v1'
                || !Array.isArray(product.regions) || !product.regions.length
                || product.regions.length > maximum) throw Error('Invalid region factory');
        const functions = [], memberCounts = [];
        for (const row of product.regions) {
            if (!row || typeof row.fn !== 'function' || !Array.isArray(row.members)
                    || row.members.length < 2 || row.members.length > 8)
                throw Error('Invalid region members');
            const seen = new Set(), origin = row.members[0]?.asid;
            for (const member of row.members) {
                if (!member || !Number.isInteger(member.asid) || member.asid < 0
                        || member.asid > 0xffffffff || member.asid !== origin
                        || !Number.isInteger(member.pc) || member.pc < 0 || member.pc > 0xffffffff
                        || (member.pc & 3) || (member.thumb !== undefined && member.thumb !== false)
                        || (member.shared !== undefined && member.shared !== false)
                        || (member.complete !== undefined && member.complete !== true)
                        || !Array.isArray(member.words) || !member.words.length || member.words.length > 128
                        || member.pc + member.words.length * 4 > 0x100000000
                        || (member.pc & 4095) + member.words.length * 4 > 4096
                        || member.words.some(word => !Number.isInteger(word) || word < 0 || word > 0xffffffff)
                        || seen.has(member.pc)) throw Error('Invalid region member');
                seen.add(member.pc);
            }
            const bytes = row.members.length * 16 + row.members.reduce((sum, member) => sum + member.words.length * 4, 0);
            const pointer = Module['_malloc'](bytes);
            if (!Number.isInteger(pointer) || pointer <= 0 || (pointer & 3) || pointer > HEAPU8.length - bytes)
                throw Error('Region manifest allocation failed');
            try {
                let words = pointer + row.members.length * 16;
                for (let index = 0; index < row.members.length; ++index) {
                    const member = row.members[index];
                    HEAPU32.set([member.asid, member.pc, words, member.words.length], (pointer >>> 2) + index * 4);
                    HEAPU32.set(member.words, words >>> 2);
                    words += member.words.length * 4;
                }
                if (!Module['_eka2l1_region_register'](pointer, row.members.length, functions.length))
                    throw Error('Region registration rejected');
            } finally { Module['_free'](pointer); }
            functions.push(row.fn);
            memberCounts.push(Object.freeze(row.members.map(member => member.words.length)));
        }
        Module['__regionFunctions'] = Object.freeze(functions);
        Module['__regionMemberCounts'] = Object.freeze(memberCounts);
        Module['__regionError'] = null;
        return functions.length;
    } catch (error) { Module['__regionError'] = String(error); return -1; }
});
EM_JS(unsigned, region_call, (ARMul_State *cpu, unsigned function_index, const void *context), {
    let active = false;
    try {
        if (Module['__regionActiveCPU'] !== undefined) throw Error('Nested region invocation');
        if (!Number.isInteger(context) || context <= 0 || (context & 3) || context > HEAPU8.length - 188)
            throw Error('Invalid region invocation context');
        const functions = Module['__regionFunctions'], counts = Module['__regionMemberCounts']?.[function_index];
        if (!Number.isInteger(function_index) || function_index < 0
                || !functions || function_index >= functions.length || !counts)
            throw Error('Invalid region function');
        const word = context >>> 2, remaining = HEAPU32[word], ready = HEAPU32[word + 5], start = HEAPU32[word + 6];
        if (!remaining || remaining > 65535 || !ready || (ready & ~((1 << counts.length) - 1))
                || start >= counts.length || !(ready & (1 << start))) throw Error('Invalid region invocation state');
        Module['__regionActiveCPU'] = cpu;
        Module['__regionActiveContext'] = context;
        active = true;
        const result = functions[function_index](cpu);
        const count = result & 65535, member = (result >>> 17) & 7, prefix = (result >>> 20) & 255;
        const complete = (result & 65536) !== 0;
        if (!Number.isInteger(result) || result <= 0 || result > 0x0fffffff
                || !count || count > remaining || member >= counts.length || !(ready & (1 << member))
                || !prefix || prefix > 128 || prefix > counts[member] || count < prefix
                || (complete ? prefix !== counts[member] : prefix === counts[member]))
            throw Error('Invalid region result');
        return result >>> 0;
    } catch (error) { Module['__regionError'] = String(error); return 0xffffffff; }
    finally {
        if (active) { delete Module['__regionActiveCPU']; delete Module['__regionActiveContext']; }
    }
});
static std::uint32_t invoke_region(ARMul_State *cpu, std::uint32_t function_index,
        const eka2l1::arm::region_js::invocation_context *context) {
    return region_call(cpu, function_index, context);
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_region_configure(int diagnostics) {
    eka2l1::arm::region_js::reset();
    region_clear(1);
    if ((diagnostics != 0 && diagnostics != 1) || !eka2l1::arm::counted_js::enabled) return -1;
    const int regions = region_setup(eka2l1::arm::counted_js::entry_limit);
    if (regions > 0) eka2l1::arm::region_js::install(invoke_region, diagnostics != 0);
    else {
        eka2l1::arm::region_js::reset();
        region_clear(0);
    }
    return regions;
}
EMSCRIPTEN_KEEPALIVE void eka2l1_region_disable() {
    eka2l1::arm::region_js::reset();
    region_clear(1);
}
EMSCRIPTEN_KEEPALIVE double eka2l1_region_calls() { return eka2l1::arm::region_js::calls(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_region_instructions() { return eka2l1::arm::region_js::instructions(); }
}
