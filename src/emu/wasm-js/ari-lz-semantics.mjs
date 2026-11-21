// Semantic operations for the original arithmetic/LZ resource decoder. These
// helpers have no live CPU binding; callers must establish code and RAM authority.
// Slot counts include condition-failed ARM instructions, but exclude the caller
// BL and import stub. The original unsigned/signed-positive division routines
// share the same quotient tail within this decoder's bounded operand domain.
export function originalDivision(dividend, divisor, signedPositive, previousR3, result) {
    if (!Number.isInteger(dividend) || dividend < 0 || dividend > 0xffffffff
        || !Number.isInteger(divisor) || divisor < 1 || divisor > 0x20000
        || (signedPositive && dividend > 0x7fffffff)) {
        throw new RangeError('Unproved original decoder division operands');
    }
    const quotient = Math.floor(dividend / divisor);
    if (quotient >= 0x100000) throw new RangeError('Unproved original decoder quotient');
    const remainder = dividend - quotient * divisor;
    let slots;
    if (signedPositive) {
        slots = quotient < 2 ? 9 : quotient < 16 ? 20 : quotient < 256 ? 34
            : quotient < 0x4000 ? 64 : 88;
    } else {
        slots = quotient < 16 ? 16 : quotient < 256 ? 30
            : quotient < 0x4000 ? 60 : 84;
    }
    result[0] = quotient;
    result[1] = remainder;
    result[2] = quotient >>> 1;
    result[3] = quotient < 256 ? previousR3 : 0;
    result[4] = ((remainder + (quotient & 3) * divisor) >>> 1) - divisor;
    result[5] = quotient >= 256 ? 0x40000000 : (quotient & 1)
        ? 0x20000000 | (remainder === 0 ? 0x40000000 : 0) : 0x80000000;
    return slots;
}

// One original mode-0 symbol, from the main loop back to the same loop head.
// Initialization, rescaling, final flush and short budgets remain original.
// Views describe already-proved contiguous normal RAM, not a virtual-memory
// resolver. Preview reads have no externally observable memory callbacks.
export function createOriginalSymbolStep({registers, object, input, output, stack, codeBase}) {
    const r = registers, o = object.view, s = stack.view, division = new Uint32Array(6);
    const decline = Symbol();
    const word = offset => o.getUint32(offset, true);
    const setWord = (offset, value) => o.setUint32(offset, value >>> 0, true);
    const stackWord = address => s.getUint32(address - stack.base, true);
    const setStackWord = (address, value) => s.setUint32(address - stack.base, value >>> 0, true);
    const readInput = address => {
        const index = address - input.base;
        if (index < 0 || index >= input.bytes.length) throw decline;
        return input.bytes[index];
    };
    function execute(budget) {
        if (!Number.isSafeInteger(budget) || budget < 0 || budget > 0xffffffff
            || r[11] - 0x38 < stack.base || r[11] > stack.base + s.byteLength
            || r[13] - 52 < stack.base || r[6] > 4095) return 0;
        const base = object.base, fp = r[11], sp = r[13], count = stackWord(fp - 0x38), total = word(0x74);
        if (r[15] !== codeBase + 0x9768 || r[7] !== base || fp - sp !== 60
            || stackWord(fp - 0x34) !== 0 || count >= total || count !== word(0x60)
            || word(0x4c) !== base + 0x84 || word(0x50) !== base + 0x570
            || word(0x54) !== base + 0xa5c || word(0x58) !== base + 0x4a60
            || word(0x5c) !== base + 0x4f48 || word(0x48) !== base + 0x5434
            || word(0x68) !== output.base || word(0x570) > 0x7ffe) return 0;
        let low = word(0x78), high = word(0x7c), value = word(0x80);
        let mask = word(0x6c), byte = word(0x70), cursor = word(0x64), normalizations = 0;
        let cost = 0, lastR1 = 0, lastIp = 0;
        const validInterval = () => Number.isInteger(low) && Number.isInteger(high)
            && Number.isInteger(value) && low >= 0 && low < high
            && high <= 0x20000 && value >= low && value < high;
        if (!validInterval() || !word(0x570)) return 0;
        const bit = () => {
            mask >>>= 1;
            if (!mask) { byte = readInput(cursor++); mask = 128; }
            return (byte & mask) ? 1 : 0;
        };
        const divide = (a, b, previousR3) => {
            try { cost += 1 + originalDivision(a >>> 0, b, false, previousR3, division); }
            catch (error) { if (error instanceof RangeError) throw decline; throw error; }
            if (cost > budget) throw decline;
            lastR1 = division[1]; lastIp = division[4];
            return division[0];
        };
        const search = (offset, maximum, scaled) => {
            let lo = 1, hi = maximum, iterations = 0;
            while (lo < hi) {
                const mid = (lo + hi) >>> 1;
                if (word(offset + mid * 4) > scaled) lo = mid + 1;
                else hi = mid;
                iterations++;
            }
            cost += 6 + iterations * 9;
            return lo;
        };
        const normalize = () => {
            let times = 0;
            if (!validInterval()) throw decline;
            for (;;) {
                if (times > 32 || cost > budget) throw decline;
                if (low >= 0x10000) {
                    low -= 0x10000; high -= 0x10000; value -= 0x10000; cost += 34;
                } else if (low >= 0x8000 && high <= 0x18000) {
                    low -= 0x8000; high -= 0x8000; value -= 0x8000; cost += 42;
                } else if (high <= 0x10000) cost += 38;
                else { cost += low < 0x8000 ? 11 : 13; break; }
                low *= 2; high *= 2; value = value * 2 + bit(); times++;
            }
            return times;
        };
        const range = high - low, sum = word(0x570);
        cost += 43;
        const scaled = divide((value - low + 1) * sum - 1, range, value - low);
        const symbol = search(0x570, 314, scaled), symbolCharacter = word(0x4f48 + symbol * 4);
        if (symbolCharacter > 313 || !word(0x84 + symbol * 4)) return 0;
        high = low + divide(range * word(0x570 + (symbol - 1) * 4), sum, base + 0x570 + symbol * 4);
        low += divide(range * word(0x570 + symbol * 4), sum, word(0x570 + symbol * 4));
        normalizations = normalize();
        const savedModelR4 = normalizations ? value : base + 0x570;
        let rank = symbol;
        while (rank > 0 && word(0x84 + rank * 4) === word(0x84 + (rank - 1) * 4)) rank--;
        if (rank < 1) return 0;
        const ties = symbol - rank, swapped = rank < symbol;
        const otherCharacter = swapped ? word(0x4f48 + rank * 4) : 0;
        if (otherCharacter > 313) return 0;
        cost += 23 + ties * 6 + (swapped ? 8 : 0) + rank * 5;
        let finalR1 = swapped ? symbolCharacter : base + 0x84 + symbol * 4;
        let finalIp = 0xffffffff, finalLr = base + 0x84;
        let length = 1, matchStart = 0, rawMatchStart = 0;
        if (symbolCharacter >= 256) {
            const positionRange = high - low, positionTotal = word(0xa5c);
            cost += 38;
            const positionScaled = divide((value - low + 1) * positionTotal - 1, positionRange, value - low);
            const position = search(0xa5c, 4096, positionScaled) - 1;
            high = low + divide(positionRange * word(0xa5c + position * 4), positionTotal, word(0xa5c + position * 4));
            low += divide(positionRange * word(0xa5c + (position + 1) * 4), positionTotal, word(0xa5c + (position + 1) * 4));
            const positionNormalizations = normalize();
            finalR1 = positionNormalizations ? mask : lastR1;
            finalIp = lastIp;
            finalLr = codeBase + (positionNormalizations ? 0x9534 : 0x94d4);
            rawMatchStart = (r[6] - position - 1) >>> 0; matchStart = rawMatchStart & 4095;
            length = symbolCharacter - 253;
            const odd = (length >>> 1) + ((count & 1) && (length & 1) ? 1 : 0);
            cost += 19 + length * 25 + odd * 5;
        } else cost += (count & 1) ? 31 : 26;
        if (count + length >= total || cost > budget || count + length > output.bytes.length) return 0;

        // Publish the original callee stack residue once, after the budget and
        // complete normal-RAM operation are known to fit.
        const saved = [r[4], r[5], r[6], r[7], r[8], r[10], fp, sp, codeBase + 0x9770, codeBase + 0x9334];
        for (let i = 0; i < saved.length; i++) setStackWord(sp - 40 + i * 4, saved[i]);
        setStackWord(sp - 52, savedModelR4); setStackWord(sp - 48, symbolCharacter); setStackWord(sp - 44, codeBase + 0x9444);
        if (swapped) {
            const other = otherCharacter;
            setWord(0x4f48 + rank * 4, symbolCharacter); setWord(0x4f48 + symbol * 4, other);
            setWord(0x4a60 + other * 4, symbol); setWord(0x4a60 + symbolCharacter * 4, rank);
        }
        setWord(0x84 + rank * 4, word(0x84 + rank * 4) + 1);
        for (let i = rank - 1; i >= 0; i--) setWord(0x570 + i * 4, word(0x570 + i * 4) + 1);
        setWord(0x78, low); setWord(0x7c, high); setWord(0x80, value);
        setWord(0x6c, mask); setWord(0x70, byte); setWord(0x64, cursor);
        let ring = r[6], written = count, last = symbolCharacter;
        for (let i = 0; i < length; i++) {
            last = symbolCharacter < 256 ? symbolCharacter : o.getUint8(0x5434 + ((matchStart + i) & 4095));
            if (!(written & 1)) s.setUint8(fp - 0x2d - stack.base, last);
            else {
                output.bytes[written - 1] = s.getUint8(fp - 0x2d - stack.base);
                output.bytes[written] = last;
            }
            o.setUint8(0x5434 + ring, last); ring = (ring + 1) & 4095; written++;
        }
        if (symbolCharacter >= 256) {
            const positionSaved = [symbolCharacter, r[5], r[6], r[7], r[8], r[10], fp, sp, codeBase + 0x9854, codeBase + 0x9458];
            for (let i = 0; i < positionSaved.length; i++) setStackWord(sp - 40 + i * 4, positionSaved[i]);
            setStackWord(sp - 44, codeBase + 0x949c);
            r[0] = rawMatchStart; r[5] = matchStart; r[8] = length; r[9] = base + 0x5434; r[10] = length;
        } else r[0] = symbolCharacter;
        setWord(0x60, written); setStackWord(fp - 0x38, written);
        r[1] = finalR1; r[2] = written; r[3] = total; r[4] = last; r[6] = ring;
        r[12] = finalIp; r[14] = symbolCharacter < 256 ? written : written - 1; r[15] = codeBase + 0x9768;
        r[16] = (r[16] & 0x0fffffff) | 0x20000000;
        return cost;
    }
    return function step(budget) {
        try { return execute(budget); }
        catch (error) { if (error === decline) return 0; throw error; }
    };
}

// The native selected-entry bridge supplies actual CPU geometry and authenticated
// MMU spans. No guest virtual address is interpreted as a host pointer here.
export function createOriginalSymbolFactory(entries) {
    return function configure({geometry, heap, resolve, diagnostics = false}) {
        if (!Array.isArray(geometry) || geometry.length !== 8 || typeof heap !== 'function'
            || typeof resolve !== 'function') throw new TypeError('Original semantic memory authority');
        const [size, reg, cpsr, n, z, c, v, t] = geometry;
        if (geometry.some(value => !Number.isSafeInteger(value) || value < 0 || value > 0xffffffff)
            || reg + 64 > size || [cpsr, n, z, c, v, t].some(offset => (offset & 3) || offset + 4 > size)
            || (reg & 3)) throw new TypeError('Original semantic CPU geometry');
        const overlap = (a, an, b, bn) => a < b + bn && b < a + an;
        const stats = diagnostics ? {contexts: 0, inputSpans: 0} : null;
        return {abi: 'original-resource-symbol-v1', stats, entries: entries.map(row => {
            const registers = new Uint32Array(17);
            let cached = null;
            function execute(cpu, budget, generation, asid, mappingLow, mappingHigh) {
                const buffer = heap().buffer, words = new Uint32Array(buffer), ri = (cpu + reg) >>> 2;
                if (!Number.isInteger(cpu) || cpu <= 0 || (cpu & 3) || cpu + size > buffer.byteLength) return 0;
                const objectAddress = words[ri + 7], fp = words[ri + 11], sp = words[ri + 13];
                if (fp - sp !== 60 || sp < 64 || sp > 0xffffff7f || objectAddress > 0xffff9b8f) return 0;
                let candidate = cached;
                if (!candidate || candidate.buffer !== buffer || candidate.cpu !== cpu || candidate.generation !== generation
                    || candidate.asid !== asid || candidate.mappingLow !== mappingLow || candidate.mappingHigh !== mappingHigh
                    || candidate.objectAddress !== objectAddress || candidate.fp !== fp || candidate.sp !== sp) candidate = null;
                if (candidate && (candidate.object.view.getUint32(0x68, true) !== candidate.output.base
                    || candidate.object.view.getUint32(0x74, true) !== candidate.total)) candidate = null;
                const span = (address, bytes, write) => {
                    if (!Number.isSafeInteger(bytes) || bytes <= 0 || bytes > 0xffffffff || address + bytes > 0x100000000) return 0;
                    const host = resolve(cpu, address, bytes, write ? 1 : 0) >>> 0;
                    return host && host + bytes <= buffer.byteLength ? host : 0;
                };
                if (!candidate) {
                    if (stats) stats.contexts++;
                    const objectHost = span(objectAddress, 0x6470, true);
                    if (!objectHost) return 0;
                    const object = {base: objectAddress, view: new DataView(buffer, objectHost, 0x6470)};
                    const outputAddress = object.view.getUint32(0x68, true), total = object.view.getUint32(0x74, true);
                    const outputHost = span(outputAddress, total, true), stackAddress = sp - 64, stackHost = span(stackAddress, 128, true);
                    if (!outputHost || !stackHost || overlap(objectHost, 0x6470, outputHost, total)
                        || overlap(objectHost, 0x6470, stackHost, 128) || overlap(outputHost, total, stackHost, 128)) return 0;
                    const input = {base: 0, bytes: new Uint8Array(0)}, output = {base: outputAddress, bytes: new Uint8Array(buffer, outputHost, total)};
                    const stack = {base: stackAddress, view: new DataView(buffer, stackHost, 128)};
                    candidate = {buffer, cpu, generation, asid, mappingLow, mappingHigh, objectAddress, fp, sp,
                        object, output, input, total, objectHost, outputHost, stackHost,
                        step: createOriginalSymbolStep({registers, object, input, output, stack, codeBase: row.codeBase})};
                }
                const cursor = candidate.object.view.getUint32(0x64, true);
                if (cursor < candidate.input.base || cursor + 8 > candidate.input.base + candidate.input.bytes.length) {
                    if (stats) stats.inputSpans++;
                    const bytes = Math.min(64, 4096 - (cursor & 4095));
                    const inputHost = span(cursor, bytes, false);
                    if (!inputHost || overlap(inputHost, bytes, candidate.objectHost, 0x6470)
                        || overlap(inputHost, bytes, candidate.outputHost, candidate.total)
                        || overlap(inputHost, bytes, candidate.stackHost, 128)) return 0;
                    candidate.input.base = cursor; candidate.input.bytes = new Uint8Array(buffer, inputHost, bytes);
                }
                cached = candidate;
                registers.set(words.subarray(ri, ri + 16)); registers[16] = words[(cpu + cpsr) >>> 2];
                const count = candidate.step(budget);
                if (!count) return 0;
                words.set(registers.subarray(0, 16), ri);
                const flags = registers[16]; words[(cpu + cpsr) >>> 2] = flags;
                words[(cpu + n) >>> 2] = flags >>> 31; words[(cpu + z) >>> 2] = (flags >>> 30) & 1;
                words[(cpu + c) >>> 2] = (flags >>> 29) & 1; words[(cpu + v) >>> 2] = (flags >>> 28) & 1;
                words[(cpu + t) >>> 2] = (flags >>> 5) & 1;
                return count;
            }
            return {asid: row.asid, pc: row.pc, words: row.words, sources: row.sources, execute};
        })};
    };
}
