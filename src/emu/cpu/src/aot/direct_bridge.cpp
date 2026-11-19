#include <cpu/aot/direct_js.h>
#include <cpu/aot/counted_js.h>
#include <cpu/dyncom/armstate.h>
#include <cstddef>
#include <emscripten/emscripten.h>

static_assert(sizeof(eka2l1::arm::direct_js::invocation_context) == 36);
static_assert(sizeof(eka2l1::arm::direct_js::configuration_layout) == 92);

EM_JS(void, direct_clear, (int clear_error), {
    if (clear_error) Module['__directError'] = null;
    delete Module['__directExecute'];
    delete Module['__directActiveCPU'];
    delete Module['__directActiveContext'];
});
EM_JS(int, direct_setup, (unsigned maximum, const void *layout), {
    try {
        const metadata = Module['__countedDirectEntries'], functions = Module['__countedFunctions'];
        if (!Array.isArray(metadata) || !metadata.length || metadata.length > maximum
                || !Object.isFrozen(metadata) || !Array.isArray(functions)
                || functions.some(fn => typeof fn !== 'function')) throw Error('Missing direct counted metadata');
        const spaces = new Map(), shared = new Map();
        for (let index = 0; index < metadata.length; ++index) {
            const row = metadata[index];
            if (!row || !Object.isFrozen(row) || row.index !== index
                    || !Number.isInteger(row.asid) || row.asid < 0 || row.asid > 0xffffffff
                    || !Number.isInteger(row.pc) || row.pc < 0 || row.pc > 0xffffffff
                    || !Number.isInteger(row.function) || row.function < 0 || row.function >= functions.length
                    || !Number.isInteger(row.words) || row.words < 1 || row.words > 128
                    || typeof row.thumb !== 'boolean' || typeof row.shared !== 'boolean'
                    || typeof row.excluded !== 'boolean' || typeof row.dispatch !== 'boolean' || (row.pc & (row.thumb ? 1 : 3)))
                throw Error('Invalid direct counted metadata');
            const key = (row.pc | (row.thumb ? 1 : 0)) >>> 0;
            let space = spaces.get(row.asid);
            if (!space) { space = new Map(); spaces.set(row.asid, space); }
            if (space.has(key) || (row.shared && shared.has(key))) throw Error('Duplicate direct counted metadata');
            space.set(key, index);
            if (row.shared) shared.set(key, index);
        }
        const range = (pointer, length) => Number.isSafeInteger(pointer) && pointer > 0
            && !(pointer & 3) && Number.isSafeInteger(length) && length > 0
            && pointer <= HEAPU8.length - length;
        if (!range(layout, 92)) throw Error('Invalid direct configuration pointer');
        if (typeof Module['__directPrepare'] === 'function') Module['__directPrepare'](maximum, layout);
        const l = layout >>> 2, heap = HEAPU32;
        const entries = heap[l], entryCount = heap[l+1], entryStride = heap[l+2];
        const loopOffset = heap[l+3], regionOffset = heap[l+4], stampStride = heap[l+5];
        const cpuSize = heap[l+6], pcOffset = heap[l+7], generationOffset = heap[l+8];
        const asidOffset = heap[l+9], tlbPointerOffset = heap[l+10], l1Offset = heap[l+11];
        const l1Stride = heap[l+12], l1PointerOffset = heap[l+13], quantumOffset = heap[l+14];
        const irqOffset = heap[l+15], cpsrOffset = heap[l+16], tflagOffset = heap[l+17];
        const aotTlbOffset = heap[l+18], codeBeginOffset = heap[l+19], codeEndOffset = heap[l+20];
        const aotExitOffset = heap[l+21], epochPointer = heap[l+22];
        const field = (offset, size = 4) => !(offset & 3) && offset <= cpuSize-size;
        if (entryCount !== metadata.length || stampStride < 24 || (stampStride & 3)
                || entryStride < 4 || (entryStride & 3) || !cpuSize || (cpuSize & 3) || cpuSize > HEAPU8.length
                || l1Stride < 12 || (l1Stride & 3) || (l1PointerOffset & 3) || l1PointerOffset+4 > l1Stride
                || (loopOffset & 3) || loopOffset+4 > entryStride
                || (regionOffset & 3) || regionOffset+4 > entryStride
                || !range(entries, entryCount*entryStride) || !range(epochPointer, 4)
                || !field(pcOffset) || !field(generationOffset) || !field(asidOffset)
                || !field(tlbPointerOffset) || !field(l1Offset, 2048*l1Stride)
                || !field(quantumOffset, 8) || !field(irqOffset) || !field(cpsrOffset)
                || !field(tflagOffset) || !field(aotTlbOffset) || !field(codeBeginOffset)
                || !field(codeEndOffset) || !field(aotExitOffset))
            throw Error('Invalid direct configuration layout');
        Module['__directExecute'] = (cpu, context) => {
            const heap = HEAPU32, bytes = HEAPU8;
            if (!Number.isInteger(context) || context <= 0 || (context & 3) || context > bytes.length-36
                    || !Number.isInteger(cpu) || cpu <= 0 || (cpu & 3) || cpu > bytes.length-cpuSize)
                throw Error('Invalid direct invocation pointers');
            const c = context >>> 2;
            const stamps = heap[c], mapping = heap[c+1], start = heap[c+2];
            const budget = heap[c+3], prefix = heap[c+4], epoch = heap[c+5];
            if (start >= entryCount || !budget || budget > 65535
                    || !stamps || (stamps & 3) || stamps > bytes.length-entryCount*stampStride
                    || (mapping && ((mapping & 3) || mapping > bytes.length-8)))
                throw Error('Invalid direct invocation context');
            const generation = cpu+generationOffset, asid = cpu+asidOffset, tlbPointer = cpu+tlbPointerOffset;
            const l1 = cpu+l1Offset, quantum = cpu+quantumOffset, irq = cpu+irqOffset;
            const cpsr = cpu+cpsrOffset, tflag = cpu+tflagOffset, aotTlb = cpu+aotTlbOffset;
            const codeBegin = cpu+codeBeginOffset, codeEnd = cpu+codeEndOffset, aotExit = cpu+aotExitOffset;
            if (heap[epochPointer >>> 2] !== epoch) throw Error('Stale direct lifetime');
            const gen = heap[generation >>> 2], mapLow = mapping ? heap[mapping >>> 2] : 0;
            const mapHigh = mapping ? heap[(mapping >>> 2)+1] : 0, actualASID = heap[asid >>> 2];
            const owner = heap[tlbPointer >>> 2], quantumValue = heap[quantum >>> 2], mode = heap[tflag >>> 2];
            const controls = heap[cpsr >>> 2] & 0x3df, tlb = heap[aotTlb >>> 2], pcWord = (cpu + pcOffset) >>> 2;
            if (mode > 1 || heap[(quantum >>> 2)+1] || prefix > quantumValue || budget > quantumValue-prefix)
                throw Error('Invalid direct quantum');
            let index = start, lastIndex = start, lastPrefix = 0, total = 0, blocks = 0, complete = false;
            while (blocks < 4096) {
                const row = metadata[index], stride = row.thumb ? 2 : 4;
                if (row.excluded || (row.thumb ? 1 : 0) !== mode || row.words > budget-total)
                    break;
                const stamp = stamps + index * stampStride;
                if (blocks) {
                    const nativeEntry = entries + index * entryStride;
                    if (heap[(nativeEntry + loopOffset) >>> 2] !== 0xffffffff
                            || heap[(nativeEntry + regionOffset) >>> 2] !== 0xffffffff) break;
                    const key = (row.pc | mode) >>> 0, slot = l1 + (((key ^ actualASID) >>> 1) & 2047) * l1Stride;
                    if (!bytes[stamp] || heap[(stamp+4) >>> 2] !== gen
                            || heap[(stamp+8) >>> 2] !== mapLow || heap[(stamp+12) >>> 2] !== mapHigh
                            || heap[slot >>> 2] !== key || heap[(slot+4) >>> 2] !== actualASID
                            || heap[(slot+l1PointerOffset) >>> 2] !== heap[(stamp+16) >>> 2]) break;
                    heap[pcWord] = row.pc;
                }
                const host = heap[(stamp+20) >>> 2];
                heap[aotTlb >>> 2] = tlb;
                heap[codeBegin >>> 2] = host;
                heap[codeEnd >>> 2] = host + row.words * stride;
                heap[aotExit >>> 2] = 0;
                const count = functions[row.function](cpu);
                if (heap[epochPointer >>> 2] !== epoch) throw Error('Direct lifetime changed during function');
                if (!Number.isInteger(count) || count < 1 || count > row.words || count > budget-total)
                    throw Error('Invalid direct function result');
                if (count < row.words && (heap[pcWord] !== row.pc + count * stride
                        || heap[tflag >>> 2] !== (row.thumb ? 1 : 0)))
                    throw Error('Invalid direct partial PC');
                total += count; ++blocks; complete = count === row.words;
                lastIndex = index; lastPrefix = count;
                if (!complete || row.dispatch || total === budget || blocks === 4096 || heap[aotExit >>> 2]
                        || heap[generation >>> 2] !== gen || heap[asid >>> 2] !== actualASID
                        || heap[tlbPointer >>> 2] !== owner || heap[quantum >>> 2] !== quantumValue
                        || heap[(quantum >>> 2)+1] || heap[tflag >>> 2] !== mode
                        || (heap[cpsr >>> 2] & 0x3df) !== controls
                        || (!heap[irq >>> 2] && !(heap[cpsr >>> 2] & 128))
                        || (mapping && (heap[mapping >>> 2] !== mapLow || heap[(mapping >>> 2)+1] !== mapHigh))) break;
                const pc = (heap[pcWord] & (mode ? 0xfffffffe : 0xfffffffc)) >>> 0, key = (pc | mode) >>> 0;
                const found = spaces.get(actualASID)?.get(key);
                index = found === undefined ? shared.get(key) : found;
                if (index === undefined) break;
            }
            if (total) { heap[c+6] = lastIndex; heap[c+7] = lastPrefix; heap[c+8] = blocks; }
            return total | (complete ? 65536 : 0);
        };
        Module['__directError'] = null;
        return 1;
    } catch (error) { Module['__directError'] = String(error); return -1; }
});
EM_JS(unsigned, direct_call, (ARMul_State *cpu, void *context), {
    let active = false;
    try {
        if (Module['__directActiveCPU'] !== undefined) throw Error('Nested direct invocation');
        if (typeof Module['__directExecute'] !== 'function') throw Error('Direct executor unavailable');
        Module['__directActiveCPU'] = cpu; Module['__directActiveContext'] = context; active = true;
        const result = Module['__directExecute'](cpu, context);
        if (!Number.isInteger(result) || result < 0 || result > 0x1ffff
                || (result === 0 ? (!Number.isInteger(context) || context <= 0 || (context & 3)
                    || context > HEAPU8.length-36 || HEAPU32[(context >>> 2)+7] || HEAPU32[(context >>> 2)+8])
                    : !(result & 65535)))
            throw Error('Invalid direct progress');
        return result >>> 0;
    } catch (error) { Module['__directError'] = String(error); return 0xffffffff; }
    finally { if (active) { delete Module['__directActiveCPU']; delete Module['__directActiveContext']; } }
});
static std::uint32_t invoke_direct(ARMul_State *cpu, eka2l1::arm::direct_js::invocation_context *context) {
    return direct_call(cpu, context);
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int eka2l1_direct_configure(int diagnostics) {
    using namespace eka2l1::arm;
    direct_js::reset(); direct_clear(1);
    if ((diagnostics != 0 && diagnostics != 1) || !counted_js::enabled) return -1;
    const auto layout = counted_js::direct_configuration();
    const int result = direct_setup(counted_js::entry_limit, &layout);
    if (result > 0) direct_js::install(invoke_direct, diagnostics != 0);
    else { direct_js::reset(); direct_clear(0); }
    return result;
}
EMSCRIPTEN_KEEPALIVE void eka2l1_direct_disable() {
    eka2l1::arm::direct_js::reset(); direct_clear(1);
}
EMSCRIPTEN_KEEPALIVE double eka2l1_direct_calls() { return eka2l1::arm::direct_js::calls(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_direct_instructions() { return eka2l1::arm::direct_js::instructions(); }
EMSCRIPTEN_KEEPALIVE double eka2l1_direct_blocks() { return eka2l1::arm::direct_js::blocks(); }
}
