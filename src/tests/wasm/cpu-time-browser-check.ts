import assert from 'node:assert/strict';
import puppeteer from 'puppeteer';
import {sampleCpuTime, cpuTimeDelta} from './cpu-time.ts';

// Exercise actual sleeping and CPU-bound browser-worker work. A wall-clock
// proxy would incorrectly charge the deliberately longer idle interval.
const browser = await puppeteer.launch({executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium',
  headless: true, args: ['--no-sandbox', '--disable-dev-shm-usage']});
try {
  const system = await browser.target().createCDPSession();
  const page = await browser.newPage();
  await page.evaluate(() => new Promise<void>(resolve => {
    const source = `onmessage = ({data}) => {
      if (data === 'idle') setTimeout(() => postMessage('done'), 1200);
      else { const end = performance.now() + 600; let n = 0;
        while (performance.now() < end) n = Math.imul(n + 1, 17);
        postMessage(n); }
    }; postMessage('ready');`;
    const worker = (globalThis as any).cpuTestWorker = new Worker(URL.createObjectURL(new Blob([source])));
    worker.onmessage = () => resolve();
  }));
  const measure = async (kind: string) => {
    const before = await sampleCpuTime(system);
    await page.evaluate(kind => new Promise(resolve => {
      const worker = (globalThis as any).cpuTestWorker;
      worker.onmessage = () => resolve(null);
      worker.postMessage(kind);
    }), kind);
    return cpuTimeDelta(before, await sampleCpuTime(system));
  };
  const idle = await measure('idle'), busy = await measure('busy');
  assert.ok(idle.renderer_complete && busy.renderer_complete);
  assert.ok(idle.host_interval_seconds > busy.host_interval_seconds);
  assert.ok(busy.renderer_cpu_seconds! > 0.25);
  assert.ok(idle.renderer_cpu_seconds! < busy.renderer_cpu_seconds! / 2);
  if (process.platform === 'linux') {
    assert.ok(busy.busiest_renderer_thread!.cpu_seconds! > 0.2);
    assert.ok(idle.busiest_renderer_thread!.cpu_seconds! < busy.busiest_renderer_thread!.cpu_seconds! / 2);
  }
  console.log(JSON.stringify({idle, busy}, null, 2));
} finally { await browser.close(); }
