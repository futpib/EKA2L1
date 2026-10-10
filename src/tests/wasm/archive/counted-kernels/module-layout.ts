// Historical instruction-count ABI only; see README.md in this directory.
// Offline call-layout experiment. Does not run or alter the game launcher.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import puppeteer from 'puppeteer';

const [directory, output] = process.argv.slice(2);
if (!directory || !output) throw Error('module-layout.ts KERNELS NEW_OUTPUT');
fs.mkdirSync(output);
const server = http.createServer((req, res) => {
    const name = path.basename(req.url || '');
    res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    if (!name) { res.end('<title>Module layout discriminator</title>'); return; }
    const file = path.join(directory, name);
    if (!fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
    fs.createReadStream(file).pipe(res);
});
await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
const rows: any[] = [];
const cases = [
    { layout: 'same', width: 1 }, { layout: 'cross', width: 1 },
    { layout: 'cross', width: 4 }, { layout: 'same', width: 4 },
    { layout: 'same', width: 8 }, { layout: 'cross', width: 8 },
    { layout: 'cross', width: 32 }, { layout: 'same', width: 32 },
    { layout: 'direct', width: 1 },
];
try {
    for (const [pc, cycles] of [[1879455500, 57], [1879129820, 7]]) {
        for (const spec of (process.env.EKA_LAYOUT_REVERSE === '1' ? [...cases].reverse() : cases)) {
            // Fresh process prevents a preceding layout's indirect-call feedback
            // and compiled code from contaminating the next case.
            const browser = await puppeteer.launch({
                executablePath: '/usr/bin/chromium', headless: true,
                args: ['--no-sandbox'], protocolTimeout: 600000,
            });
            try {
                const page = await browser.newPage();
                await page.goto(`http://127.0.0.1:${(server.address() as any).port}/`);
                const row = await page.evaluate(async ({ pc, cycles, layout, width }) => {
                    const fixtures = (await (await fetch('/native-fixtures.json')).json())[String(pc)];
                    const memory = new WebAssembly.Memory({ initial: 256, maximum: 32768, shared: true });
                    const table = new WebAssembly.Table({ initial: 32, element: 'anyfunc' });
                    const bytes = new Uint8Array(memory.buffer), words = new Uint32Array(memory.buffer);
                    const fail = () => { throw Error('Unexpected ordinary-memory fixture fallback'); };
                    const env = { memory, table, tlb_read32: fail, tlb_write32: fail,
                        tlb_read8: fail, tlb_write8: fail, tlb_read16: fail, tlb_write16: fail };
                    const module = await WebAssembly.compile(await (await fetch(`/${pc}.wasm`)).arrayBuffer());
                    const local = (await WebAssembly.instantiate(module, { env })).exports as any;
                    const other = (await WebAssembly.instantiate(module, { env })).exports as any;
                    const target = layout === 'cross' ? other : local;
                    for (let i = 0; i < 32; ++i) table.set(i, target[`kernel${i}`]);
                    const state = 1024;
                    function init(seed: number, budget: number) {
                        bytes.fill(0);
                        let x = seed;
                        for (let address = 0x10000; address < 0x19000; address += 4) {
                            x = (Math.imul(x, 1664525) + 1013904223) >>> 0;
                            words[address / 4] = x;
                        }
                        for (let r = 0; r < 16; ++r) words[state / 4 + r] = 0x13000 + r * 64;
                        for (const [r, value] of [[0, 0x12000], [1, 0x10000], [2, 0x11000],
                            [4, 0x13000], [5, 0x14000], [13, 0x18000], [14, 0x1a000], [15, pc]])
                            words[state / 4 + r] = value;
                        for (const [offset, value] of [[784, 16], [796, 16], [848, budget],
                            [852, 8192], [856, 0x70000000], [860, 0x70080000], [876, 1]])
                            words[(state + offset) / 4] = value;
                        for (let address = 0x10000; address <= 0x19000; address += 4096) {
                            const entry = (8192 + ((address >>> 12) & 511) * 16) / 4;
                            words.fill(address, entry, entry + 4);
                        }
                    }
                    function verify(fixture: any, count: number, expected: Uint8Array) {
                        if (count !== fixture.budget) throw Error(`Count ${count} != ${fixture.budget}`);
                        for (let r = 0; r < 16; ++r)
                            if (words[state / 4 + r] !== fixture.regs[r]) throw Error(`R${r} mismatch`);
                        const cpsr = (words[(state + 784) / 4] & 0x0fffffdf)
                            | (words[(state + 804) / 4] << 31) | (words[(state + 808) / 4] << 30)
                            | (words[(state + 812) / 4] << 29) | (words[(state + 816) / 4] << 28)
                            | (words[(state + 828) / 4] << 5);
                        if ((cpsr >>> 0) !== fixture.cpsr) throw Error('CPSR mismatch');
                        for (let i = 0; i < expected.length; ++i)
                            if (bytes[i + 0x10000] !== expected[i]) throw Error(`Memory mismatch ${i}`);
                    }
                    let comparisons = 0;
                    for (const fixture of fixtures) {
                        init(fixture.seed, fixture.budget);
                        const expected = bytes.slice(0x10000, 0x20000);
                        for (const [address, value] of fixture.changes) expected[address - 0x10000] = value;
                        // Every duplicate is checked at full budget. Kernel zero
                        // additionally checks every seeded partial-budget state.
                        for (let i = 0; i < (fixture.budget === cycles ? 32 : 1); ++i) {
                            init(fixture.seed, fixture.budget);
                            verify(fixture, target[`kernel${i}`](state), expected);
                            ++comparisons;
                        }
                        init(fixture.seed, fixture.budget);
                        bytes.copyWithin(4096, state, state + 64);
                        verify(fixture, local[layout === 'direct' ? 'direct' : 'indirect'](1, width - 1), expected);
                        ++comparisons;
                    }
                    const loop = local[layout === 'direct' ? 'direct' : 'indirect'];
                    init(72, cycles);
                    bytes.copyWithin(4096, state, state + 64);
                    if (loop(500000, width - 1) !== cycles * 500000) throw Error('Warmup count');
                    await new Promise(resolve => setTimeout(resolve, 1000));
                    const timings = [];
                    for (let round = 0; round < 8; ++round) {
                        init(72, cycles);
                        bytes.copyWithin(4096, state, state + 64);
                        const start = performance.now();
                        const count = loop(5000000, width - 1);
                        const ms = performance.now() - start;
                        if (count !== cycles * 5000000) throw Error('Timed count');
                        timings.push(ms);
                    }
                    return { pc, cycles, layout, width, comparisons, calls: 5000000,
                        userAgent: navigator.userAgent, timings };
                }, { pc, cycles, ...spec });
                rows.push(row);
                fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(rows, null, 2) + '\n');
                console.log(JSON.stringify(row));
            } finally { await browser.close(); }
        }
    }
} finally { server.close(); }
