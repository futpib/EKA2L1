// Offline byte-comparison discriminator. No CPU mapping/invalidation integration.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import os from 'node:os';
import puppeteer from 'puppeteer';

const [build, directory, output] = process.argv.slice(2);
if (!output) throw Error('snapshot-validators.ts HELPER_BUILD MODULES NEW_OUTPUT');
fs.mkdirSync(output);
const hash = (file: string) => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const manifest = JSON.parse(fs.readFileSync(path.join(directory, 'manifest.json'), 'utf8'));
for (const c of manifest.cases)
    if (hash(path.join(directory, `${c.size}.wasm`)) !== c.wasm_sha256) throw Error('Module hash mismatch');
const compareMode = Number(process.env.EKA_SNAPSHOT_COMPARE || '2');
if (![0,1,2,3].includes(compareMode)) throw Error('Invalid comparison mode');
const checkOnly = process.env.EKA_SNAPSHOT_CHECK_ONLY === '1';
const provenance = { manifest, compareMode, checkOnly, helperWasm: hash(path.join(build, 'eka_matched_kernel.wasm')),
    helperJs: hash(path.join(build, 'eka_matched_kernel.js')), cpu: os.cpus()[0].model,
    reverse: process.env.EKA_SNAPSHOT_REVERSE === '1', crossControl: process.env.EKA_SNAPSHOT_CROSS === '1', source: hash(import.meta.filename) };
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
const report: any = { provenance, checks: null, rows: [] };
const save = () => fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
async function run(spec: any) {
    const browser = await puppeteer.launch({ executablePath: '/usr/bin/chromium', headless: true,
        args: ['--no-sandbox'], protocolTimeout: 600000 });
    try {
        const page = await browser.newPage();
        const errors: string[] = [];
        page.on('pageerror', e => errors.push(String(e)));
        await page.goto(`http://127.0.0.1:${(server.address() as any).port}/`);
        const result = await page.evaluate(async ({ spec, cases, compareMode }) => {
            const setupStart = performance.now();
            const m = await (window as any).createMatchedKernel();
            m._layout_expose(); m._layout_compare_mode(compareMode);
            const setupMs = performance.now() - setupStart;
            const base = m._data_pointer(), heap = m.HEAPU8;
            const a = base + 64, b = base + 8192, noise = base + 0x18000;
            let comparisons = 0;
            const load = async (c: any) => {
                const bytes = await (await fetch(`/${c.size}.wasm`)).arrayBuffer();
                const start = performance.now();
                const module = await WebAssembly.compile(bytes);
                const compileMs = performance.now() - start;
                const startInstance = performance.now();
                const instance = await WebAssembly.instantiate(module,
                    { env: { memory: m.layoutMemory, compare: m.layoutCompare } });
                return { module, exports: instance.exports as any, compileMs,
                    instantiateMs: performance.now() - startInstance };
            };
            const expected = (c: any) => Uint8Array.from(c.expected.match(/../g) || [], (h: any) => parseInt(h, 16));
            if (spec.check) {
                for (const c of cases) {
                    const { exports: f } = await load(c), data = expected(c), n = c.size;
                    const verify = (ap: number, bp: number) => {
                        const truth = data.every((v, i) => heap[ap + i] === v) ? 1 : 0;
                        const genericTruth = data.every((_, i) => heap[ap + i] === heap[bp + i]) ? 1 : 0;
                        for (const [name, value, want] of [
                            ['constant', f.constant(ap, bp, n), truth],
                            ['generic', f.generic(ap, bp, n), genericTruth],
                            ['production', m.layoutCompare(ap, bp, n), genericTruth]]) {
                            ++comparisons;
                            if (value !== want) throw Error(`${name} n=${n} ${value} != ${want}`);
                        }
                    };
                    for (let align = 0; align < 16; ++align) {
                        const ap = a + align, bp = b + (15 - align);
                        heap.set(data, ap); heap.set(data, bp); verify(ap, bp);
                        for (let i = 0; i < n; ++i) {
                            heap[ap + i] ^= 0x80; verify(ap, bp); heap[ap + i] ^= 0x80;
                        }
                        verify(ap, bp);
                        if (n) { heap[bp + n - 1] ^= 1; verify(ap, bp); heap[bp + n - 1] ^= 1; }
                        if (n >= 64) {
                            heap[ap] ^= 1; heap[ap + 16] ^= 1; verify(ap, bp);
                            heap[ap] ^= 1; heap[ap + 16] ^= 1;
                        }
                    }
                    // Any load beyond the exact span traps here, including zero-size reads.
                    const edge = heap.length - n;
                    heap.set(data, edge); heap.set(data, b); verify(edge, b);
                    heap.set(data, a); verify(a, edge);
                    heap.set(data, a); heap.set(data, b);
                    for (const kind of ['production', 'generic', 'constant']) {
                        heap[noise] = 123;
                        if (f[`loop_${kind}`](a, b, noise, 0) !== 0 || heap[noise] !== 123)
                            throw Error('Zero-count loop has effects');
                        if (f[`loop_${kind}`](a, b, noise, 17) !== 17 || heap[noise] !== 16)
                            throw Error('Loop result/noise mismatch');
                        ++comparisons;
                    }
                }
                return { comparisons, cases: cases.length, setupMs, userAgent: navigator.userAgent };
            }
            const c = cases.find((c: any) => c.size === spec.size);
            const loaded = await load(c), data = expected(c);
            let f = loaded.exports[`loop_${spec.kind}`], crossInstantiateMs = 0;
            if (spec.kind.endsWith('_cross')) {
                const start = performance.now();
                const cross = await WebAssembly.instantiate(loaded.module, { env: { memory: m.layoutMemory,
                    compare: loaded.exports[spec.kind.replace('_cross', '')] } });
                crossInstantiateMs = performance.now() - start;
                f = cross.exports.loop_production;
            }
            heap.set(data, a); heap.set(data, b);
            const timed = (count: number) => {
                const start = performance.now(), sum = f(a, b, noise, count), ms = performance.now() - start;
                if (sum !== count || heap[noise] !== ((count - 1) & 255)) throw Error('Timed output mismatch');
                return ms;
            };
            const firstMs = timed(1), warmMs = timed(500000), timings = [];
            for (let i = 0; i < 8; ++i) timings.push(timed(5000000));
            if (!data.every((v, i) => heap[a+i] === v && heap[b+i] === v)) throw Error('Input changed');
            return { ...spec, setupMs, compileMs: loaded.compileMs, instantiateMs: loaded.instantiateMs,
                crossInstantiateMs, firstMs, warmMs, timings, calls: 5000000, bytes: c.bytes, userAgent: navigator.userAgent };
        }, { spec, cases: manifest.cases, compareMode });
        if (errors.length) throw Error(errors.join('\n'));
        return result;
    } finally { await browser.close(); }
}
try {
    report.checks = await run({ check: true }); save(); console.log(JSON.stringify(report.checks));
    const kinds = provenance.crossControl ? ['production', 'generic_cross', 'constant_cross'] : ['production', 'generic', 'constant'];
    const cases = [8, 28, 64, 228, 512].flatMap(size => kinds.map(kind => ({size, kind})));
    for (const spec of (checkOnly ? [] : provenance.reverse ? cases.reverse() : cases)) {
        const row = await run(spec); report.rows.push(row); save(); console.log(JSON.stringify(row));
    }
} finally { server.close(); }
