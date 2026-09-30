// Verify real launcher downloads on cold load, reload and browser restart.
import fs from 'node:fs';
import path from 'node:path';
import puppeteer from 'puppeteer';
import { startServer, compilerPolicyFromEnv } from './server.ts';
const [assets, output, existingUrl] = process.argv.slice(2);
if (!output) throw Error('cache-live.ts ASSETS NEW_OUTPUT [EXISTING_URL]');
fs.mkdirSync(output);
const local = existingUrl ? null : await startServer(0, {
  '/preload/rom': path.join(assets, 'SYM.ROM'), '/preload/rpkg': path.join(assets, 'SYM.RPKG'),
  '/preload/sis': path.join(assets, 'Snakes.sis'),
}, 'Snakes', { compilerPolicy: compilerPolicyFromEnv() });
const url = existingUrl || `http://127.0.0.1:${local!.port}/`;
const rows: any[] = [], errors: string[] = [];
let browser: any;
const launch = () => puppeteer.launch({ executablePath: '/usr/bin/chromium', headless: true,
  userDataDir: path.join(output, 'profile'), args: ['--no-sandbox', '--disable-dev-shm-usage',
    '--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist'] });
try {
  browser = await launch();
  let page = await browser.newPage();
  for (const phase of ['cold', 'reload', 'restart']) {
    if (phase === 'restart') {
      await browser.close(); browser = await launch(); page = await browser.newPage();
    }
    const client = await page.createCDPSession(); await client.send('Network.enable');
    const requests = new Map<string, any>();
    client.on('Network.responseReceived', ({ requestId, response }: any) => requests.set(requestId, {
      url: response.url, status: response.status, fromDiskCache: response.fromDiskCache || false,
      fromServiceWorker: response.fromServiceWorker || false, headers: response.headers, transferred: null,
    }));
    client.on('Network.loadingFinished', ({ requestId, encodedDataLength }: any) => {
      const r = requests.get(requestId); if (r) r.transferred = encodedDataLength;
    });
    const pageError = (e: any) => errors.push(String(e));
    const requestError = (r: any) => errors.push(r.url() + ': ' + r.failure()?.errorText);
    page.on('pageerror', pageError); page.on('requestfailed', requestError);
    const start = Date.now();
    if (phase === 'reload') await page.reload({ waitUntil: 'domcontentloaded' });
    else await page.goto(url, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(() => (window as any)._gameRunning, { timeout: 180000 });
    const state = await page.evaluate(async () => {
      const w = window as any, cache = await caches.open('eka2l1-downloads-v1');
      const saved = [];
      for (const request of await cache.keys()) {
        const response = await cache.match(request);
        saved.push({ url: request.url, bytes: Number(response!.headers.get('content-length')) });
      }
      return { secure: isSecureContext, isolated: crossOriginIsolated, saved, urls: w.ekaAssetUrls,
        policy: w.ekaCompilerPolicy, guest: w.Module._eka2l1_guest_time_us() };
    });
    if (!state.secure || !state.isolated || !state.policy?.applied) throw Error('Launch/security/policy');
    if (state.saved.length !== 3 || state.saved.some(x => x.bytes <= 0)) throw Error('Large files not stored');
    const transfers = [...requests.values()];
    const preloads = transfers.filter(x => new URL(x.url).pathname.startsWith('/preload/'));
    const binary = transfers.filter(x => /\.(?:wasm|js|data)$/.test(new URL(x.url).pathname));
    if (phase === 'cold' && preloads.length !== 3) throw Error('Cold download coverage');
    if (phase !== 'cold' && preloads.length !== 0) throw Error('Repeated preload network fetch');
    if (phase !== 'cold' && binary.some(x => x.transferred === null || x.transferred > 1024))
      throw Error('Repeated runtime body transfer: ' + JSON.stringify(binary));
    if (transfers.some(x => x.status >= 400)) throw Error('HTTP failure');
    rows.push({ phase, elapsedToLaunchMs: Date.now() - start, state, transfers });
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ url, rows, errors }, null, 2));
    await page.evaluate(() => { const w = window as any; w._gameRunning = false; w.Module._eka2l1_shutdown(); });
    page.off('pageerror', pageError); page.off('requestfailed', requestError); await client.detach();
    console.log(phase + ': launched, persistent assets ' + state.saved.reduce((n, x) => n + x.bytes, 0)
      + ' bytes; preload network requests ' + preloads.length + '; runtime transferred '
      + binary.reduce((n, x) => n + (x.transferred || 0), 0));
  }
  if (errors.length) throw Error(errors.join('\n'));
} finally { await browser?.close(); local?.server.close(); }
