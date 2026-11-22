// Complete iterations of the authenticated original callback-list scan only.
// Exit nodes, short budgets and non-normal RAM remain in the original CPU.
export function createListScanFactory(entries) {
    return function configure({geometry, shifterCarry, heap, resolve, diagnostics = false}) {
        if (!Array.isArray(geometry) || geometry.length !== 8 || typeof heap !== 'function'
            || typeof resolve !== 'function') throw new TypeError('List scan memory authority');
        const [size, reg, cpsr, n, z, c, v, t] = geometry;
        if ([...geometry, shifterCarry].some(x => !Number.isSafeInteger(x) || x < 0 || x > 0xffffffff)
            || (reg & 3) || reg + 64 > size
            || [cpsr, n, z, c, v, t, shifterCarry].some(x => (x & 3) || x + 4 > size))
            throw new TypeError('List scan CPU geometry');
        const stats = diagnostics ? {calls: 0, iterations: 0, pages: 0} : null;
        return {abi: 'original-resource-symbol-v1', stats, entries: entries.map(row => {
            const pages = new Uint32Array(64), hosts = new Uint32Array(64);
            let buffer, owner, gen, space, low, high;
            function execute(cpu, budget, generation, asid, mappingLow, mappingHigh) {
                const bytes = heap(), words = new Uint32Array(bytes.buffer);
                if (!Number.isInteger(cpu) || cpu <= 0 || (cpu & 3) || cpu + size > bytes.byteLength
                    || !Number.isInteger(budget) || budget < 7 || budget > 0xffffffff) return 0;
                const ri = (cpu + reg) >>> 2;
                if (words[ri + 15] !== row.pc || words[(cpu + t) >>> 2]) return 0;
                if (buffer !== bytes.buffer || owner !== cpu || gen !== generation || space !== asid
                    || low !== mappingLow || high !== mappingHigh) {
                    hosts.fill(0); buffer = bytes.buffer; owner = cpu; gen = generation;
                    space = asid; low = mappingLow; high = mappingHigh;
                }
                const sentinel = words[ri + 4], limit = Math.min(Math.floor(budget / 7), 1024);
                let link = words[ri + 14], count = 0, r0, r2, ip, r3 = words[ri + 3], carry, overflow;
                while (count < limit && link !== sentinel) {
                    // LDMDA reads three aligned words ending at the old link.
                    const offset = link & 4095;
                    if ((link & 3) || offset < 8) break;
                    const page = (link & 0xfffff000) >>> 0, slot = (page >>> 12) & 63;
                    let host = hosts[slot];
                    if (!host || pages[slot] !== page) {
                        host = resolve(cpu, page, 4096, 0) >>> 0;
                        if (!host || (host & 3) || host + 4096 > bytes.byteLength) break;
                        pages[slot] = page; hosts[slot] = host; if (stats) stats.pages++;
                    }
                    const index = (host + offset) >>> 2;
                    const kind = words[index - 2], flags = words[index - 1], next = words[index];
                    if (kind !== 0x80000001 && (flags & 1)) break;
                    r0 = (link - 12) >>> 0; r2 = kind; ip = flags; link = next;
                    carry = kind >= 0x80000001 ? 1 : 0;
                    overflow = kind > 0 && kind < 0x80000000 ? 1 : 0;
                    if (kind !== 0x80000001) r3 = 0;
                    count++;
                }
                if (!count) return 0;
                words[ri] = r0; words[ri + 2] = r2; words[ri + 3] = r3;
                words[ri + 12] = ip; words[ri + 14] = link;
                words[(cpu + cpsr) >>> 2] = (words[(cpu + cpsr) >>> 2] & 0x0fffffff)
                    | 0x40000000 | (carry << 29) | (overflow << 28);
                words[(cpu + n) >>> 2] = 0; words[(cpu + z) >>> 2] = 1;
                words[(cpu + c) >>> 2] = carry; words[(cpu + v) >>> 2] = overflow;
                words[(cpu + shifterCarry) >>> 2] = carry;
                if (stats) { stats.calls++; stats.iterations += count; }
                return count * 7;
            }
            return {...row, originalFallback: true, execute};
        })};
    };
}
