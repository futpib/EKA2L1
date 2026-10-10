// Historical instruction-count ABI only; see README.md in this directory.
// Offline dispatch discriminator with the real C++ code-cache validator.
// Clones share one PC/snapshot. This is not an emulator execution-chain test.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import puppeteer from 'puppeteer';

const [build, directory, output] = process.argv.slice(2);
if (!output) throw Error('validated-layout.ts HELPER_BUILD MODULES NEW_OUTPUT');
fs.mkdirSync(output);
const hash = (file: string) => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const provenance = {
    helperWasm: hash(path.join(build, 'eka_matched_kernel.wasm')),
    helperJs: hash(path.join(build, 'eka_matched_kernel.js')),
    manifest: JSON.parse(fs.readFileSync(path.join(directory, 'manifest.json'), 'utf8')),
    fixtureSha256: hash(path.join(directory, 'native-fixtures.json')),
    reverse: process.env.EKA_LAYOUT_REVERSE === '1',
    checkOnly: process.env.EKA_LAYOUT_CHECK_ONLY === '1',
};
for (const k of provenance.manifest.kernels)
    if (hash(path.join(directory, `${k.pc}.wasm`)) !== k.module_sha256) throw Error('Module hash mismatch');
const server = http.createServer((req, res) => {
    const name = path.basename(req.url || '');
    res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    if (!name) { res.end('<script src="/eka_matched_kernel.js"></script>'); return; }
    if (name === 'favicon.ico') { res.writeHead(204).end(); return; }
    const file = path.join(name.startsWith('eka_matched_kernel') ? build : directory, name);
    if (!fs.existsSync(file)) { res.writeHead(404).end(); return; }
    if (name.endsWith('.wasm')) res.setHeader('Content-Type', 'application/wasm');
    fs.createReadStream(file).pipe(res);
});
await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
const rows: any[] = [];
const cases = [
    { layout: 'direct', width: 1 }, { layout: 'same', width: 1 }, { layout: 'cross', width: 1 },
    { layout: 'cross', width: 8 }, { layout: 'same', width: 8 }, { layout: 'direct', width: 8 },
    { layout: 'direct', width: 32 }, { layout: 'same', width: 32 }, { layout: 'cross', width: 32 },
];
try {
    for (const [pc, cycles] of [[1879455500, 57], [1879129820, 7]]) {
        for (const spec of (provenance.reverse ? [...cases].reverse() : cases)) {
            const browser = await puppeteer.launch({ executablePath: '/usr/bin/chromium', headless: true,
                args: ['--no-sandbox'], protocolTimeout: 600000 });
            try {
                const page = await browser.newPage();
                page.on('pageerror', e => console.error(e));
                await page.goto(`http://127.0.0.1:${(server.address() as any).port}/`);
                const row = await page.evaluate(async ({ pc, cycles, layout, width, checkOnly }) => {
                    const m = await (window as any).createMatchedKernel();
                    m._select_kernel(pc === 1879129820 ? 1 : 0);
                    m._layout_expose();
                    const fixtures = (await (await fetch('/native-fixtures.json')).json())[String(pc)];
                    const table = new WebAssembly.Table({ initial: 32, element: 'anyfunc' });
                    const fail = () => { throw Error('Unexpected ordinary-memory fixture fallback'); };
                    const env = { memory: m.layoutMemory, table, validate: m.layoutValidate,
                        tlb_read32: fail, tlb_write32: fail, tlb_read8: fail, tlb_write8: fail,
                        tlb_read16: fail, tlb_write16: fail };
                    const module = await WebAssembly.compile(await (await fetch(`/${pc}.wasm`)).arrayBuffer());
                    const local = (await WebAssembly.instantiate(module, { env })).exports as any;
                    const other = (await WebAssembly.instantiate(module, { env })).exports as any;
                    const target = layout === 'cross' ? other : local;
                    for (let i = 0; i < 32; ++i) table.set(i, target[`kernel${i}`]);
                    const state = m._layout_state(), base = m._data_pointer(), regs = m._register_pointer();
                    const loop = local[layout === 'direct' ? 'direct' : 'indirect'];
                    function init(seed: number, budget: number) {
                        m._reset(seed, budget); m._layout_capture(); m._layout_reset_cache();
                    }
                    function verify(f: any, count: number, expected: Uint8Array) {
                        if (count !== f.budget) throw Error(`Count ${count} != ${f.budget}`);
                        for (let r = 0; r < 16; ++r)
                            if (m.HEAPU32[(regs >>> 2) + r] !== f.regs[r]) throw Error(`R${r} mismatch`);
                        if ((m._cpsr_value() >>> 0) !== f.cpsr) throw Error('CPSR mismatch');
                        for (let i = 0; i < expected.length; ++i)
                            if (m.HEAPU8[base + i] !== expected[i]) throw Error(`Memory mismatch ${i}`);
                    }
                    let comparisons = 0;
                    for (const f of fixtures) {
                        init(f.seed, f.budget);
                        const expected = m.HEAPU8.slice(base, base + 0x20000);
                        for (const [address, value] of f.changes) expected[address] = value;
                        for (let i = 0; i < (f.budget === cycles ? 32 : 1); ++i) {
                            init(f.seed, f.budget);
                            if (!m.layoutValidate(state)) throw Error('Fresh validation failed');
                            verify(f, target[`kernel${i}`](state), expected); ++comparisons;
                        }
                        init(f.seed, f.budget);
                        verify(f, loop(state, 1, width - 1), expected); ++comparisons;
                    }
                    let rejectionChecks = 0;
                    for (let mode = 1; mode <= 5; ++mode) {
                        init(17, cycles);
                        if (!m.layoutValidate(state)) throw Error('Fresh mapping failed');
                        const expected = m.HEAPU8.slice(base, base + 0x20000);
                        m._layout_mutate(mode);
                        if (loop(state, 1, width - 1) !== 0) throw Error(`Failed to reject mutation ${mode}`);
                        for (let i = 0; i < expected.length; ++i)
                            if (m.HEAPU8[base + i] !== expected[i]) throw Error('Rejected code wrote memory');
                        if (m.HEAPU32[(regs >>> 2) + 15] !== pc) throw Error('Rejected code advanced PC');
                        ++rejectionChecks;
                    }
                    init(72, cycles);
                    if (loop(state, 0, width - 1) !== 0 || m._layout_mapping_calls() !== 0)
                        throw Error('Zero-call loop dispatched');
                    const warmCalls = checkOnly ? 100 : 500000;
                    if (loop(state, warmCalls, width - 1) !== cycles * warmCalls) throw Error('Warmup count');
                    if (m._layout_mapping_calls() !== 1) throw Error('Stable mapping was not reused');
                    const timings = [], mappingCalls = [];
                    for (let round = 0; round < (checkOnly ? 0 : 8); ++round) {
                        init(72, cycles);
                        const start = performance.now();
                        const count = loop(state, 5000000, width - 1);
                        timings.push(performance.now() - start);
                        if (count !== cycles * 5000000) throw Error('Timed count');
                        mappingCalls.push(m._layout_mapping_calls());
                        if (mappingCalls.at(-1) !== 1) throw Error('Unexpected mapping refreshes');
                    }
                    return { pc, cycles, layout, width, comparisons, rejectionChecks, timings, mappingCalls,
                        calls: 5000000, userAgent: navigator.userAgent };
                }, { pc, cycles, ...spec, checkOnly: provenance.checkOnly });
                rows.push(row);
                fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ provenance, rows }, null, 2) + '\n');
                console.log(JSON.stringify(row));
            } finally { await browser.close(); }
        }
    }
} finally { server.close(); }
