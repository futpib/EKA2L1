import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import puppeteer from 'puppeteer';
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'eka-cache-test-'));
const build = path.join(temp, 'build'); fs.mkdirSync(build);
fs.writeFileSync(path.join(build, 'eka2l1.html'), '<html><head></head><body><script src=eka2l1.js></script></body></html>');
fs.writeFileSync(path.join(build, 'eka2l1.js'), 'window.fixtureLoaded=true;');
fs.writeFileSync(path.join(build, 'eka2l1.wasm'), 'fixture-wasm');
const rom = path.join(temp, 'rom'), sis = path.join(temp, 'sis');
fs.writeFileSync(rom, 'original-rom'); fs.writeFileSync(sis, 'original-sis');
process.env.EKA2L1_WASM_BUILD_DIR = build;
const { startServer } = await import('./server.ts');
const { server, port } = await startServer(0, { '/preload/rom': rom, '/preload/sis': sis });
const origin = `http://127.0.0.1:${port}`;
let browser;
try {
  const root = await fetch(origin), html = await root.text();
  assert.equal(root.headers.get('cache-control'), 'no-cache');
  const urls = JSON.parse(html.match(/window.ekaAssetUrls = (.*);/)![1]);
  assert.match(html, /src="\/eka2l1.js\?v=[a-f0-9]{64}"/);
  const asset = await fetch(origin + urls['/preload/rom']);
  assert.equal(await asset.text(), 'original-rom');
  assert.match(asset.headers.get('cache-control')!, /max-age=31536000, immutable/);
  assert.equal(asset.headers.get('content-length'), '12');
  assert.equal((await fetch(origin + urls['/preload/rom'], { headers: { 'If-None-Match': asset.headers.get('etag')! } })).status, 304);
  const head = await fetch(origin + urls['/preload/rom'], { method: 'HEAD' });
  assert.equal(head.headers.get('content-length'), '12'); assert.equal(await head.text(), '');
  assert.equal((await fetch(origin, { headers: { 'If-None-Match': root.headers.get('etag')! } })).status, 304);
  assert.equal((await fetch(origin + '/eka2l1.wasm?v=wrong')).status, 409);
  assert.equal((await fetch(origin, { method: 'POST' })).status, 405);
  const launch = () => puppeteer.launch({ executablePath: '/usr/bin/chromium', headless: true,
    args: ['--no-sandbox'], userDataDir: path.join(temp, 'profile') });
  browser = await launch();
  let page = await browser.newPage(); await page.goto(origin);
  assert.equal(await page.evaluate(() => (window as any).fixtureLoaded), true);
  assert.equal(await page.evaluate(async () => (await (window as any).ekaDownload('/preload/rom')).text()), 'original-rom');
  await browser.close(); browser = await launch(); page = await browser.newPage(); await page.goto(origin);
  await page.setOfflineMode(true);
  assert.equal(await page.evaluate(async () => (await (window as any).ekaDownload('/preload/rom')).text()), 'original-rom');
  await page.setOfflineMode(false);
  await new Promise(resolve => setTimeout(resolve, 10)); fs.writeFileSync(rom, 'replaced-rom');
  assert.equal((await fetch(origin + urls['/preload/rom'])).status, 409);
  assert.equal((await fetch(origin, { headers: { 'If-None-Match': root.headers.get('etag')! } })).status, 200);
  await page.reload();
  assert.equal(await page.evaluate(async () => (await (window as any).ekaDownload('/preload/rom')).text()), 'replaced-rom');
  const keys = await page.evaluate(async () => (await (await caches.open('eka2l1-downloads-v1')).keys()).map(k => k.url));
  assert.equal(keys.length, 1); assert.notEqual(keys[0], origin + urls['/preload/rom']);
  assert.equal(await page.evaluate(async () => {
    const original = Cache.prototype.put;
    Cache.prototype.put = async () => { throw new DOMException('full', 'QuotaExceededError'); };
    try { return await (await (window as any).ekaDownload('/preload/sis')).text(); }
    finally { Cache.prototype.put = original; }
  }), 'original-sis');
  assert.equal(await page.evaluate(async () => {
    const original = CacheStorage.prototype.open;
    CacheStorage.prototype.open = async () => { throw new Error('storage blocked'); };
    try { return await (await (window as any).ekaDownload('/preload/sis')).text(); }
    finally { CacheStorage.prototype.open = original; }
  }), 'original-sis');
  assert.equal(await page.evaluate(async () => (await (window as any).ekaDownload('/preload/missing')).status), 404);
  console.log('PASS versioned HTTP/HEAD/304, changed assets, persistent browser cache across restart/offline, pruning and storage-failure fallback');
} finally {
  await browser?.close(); await new Promise<void>(resolve => server.close(() => resolve()));
  fs.rmSync(temp, { recursive: true, force: true });
}
