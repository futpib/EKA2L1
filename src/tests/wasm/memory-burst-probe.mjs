import fs from 'node:fs';
import path from 'node:path';
import puppeteer from 'puppeteer';
import {ChromeMemoryProfiler} from './chrome-memory-profiler.mjs';

const [game, output, serverUrl] = process.argv.slice(2);
if (!game || !output || !serverUrl) throw Error('Usage: node memory-burst-probe.mjs GAME OUTPUT SERVER_URL');
const games = JSON.parse(fs.readFileSync(new URL('./games.json', import.meta.url)));
const entry = games.find(item => item.id === game);
if (!entry) throw Error(`Unknown game: ${game}`);
fs.mkdirSync(output);
const livePath = process.env.EKA2L1_BURST_LIVE_PATH === '1';
const nativeProfiling = process.env.EKA2L1_BURST_NATIVE_PROFILE !== '0';
const controlPage = process.env.EKA2L1_BURST_CONTROL_PAGE === '1';
const manualLive = process.env.EKA2L1_BURST_MANUAL_LIVE === '1';
const anchors = process.env.EKA2L1_BURST_ANCHORS?.split(',') ??
  (manualLive ? ['asset-load', 'game-launch', 'device-install'] : ['page-navigation', 'game-launch']);
const cumulative = process.env.EKA2L1_BURST_CUMULATIVE === '1';
const minimumKiB = Number(process.env.EKA2L1_BURST_MIN_MIB ?? 32) * 1024;

const browser = await puppeteer.launch({
  executablePath: process.env.EKA2L1_CHROMIUM || '/usr/bin/chromium', headless: true,
  args: ['--host-resolver-rules=MAP claude-laptop.lan 127.0.0.1', '--no-sandbox',
    '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=vulkan',
    '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist',
    '--disable-background-timer-throttling']
});
const page = await browser.newPage();
page.setDefaultTimeout(180000);
const system = await browser.target().createCDPSession();
const profiler = nativeProfiling ? new ChromeMemoryProfiler(page, output) : null;
const samples = [], marks = [], phaseCaptures = [];
let stage = 'blank', sampling = false, closed = false, lastStatus;
const mark = name => { stage = name; marks.push({ms: performance.now(), name}); };
page.on('console', message => {
  if (message.text() === '[memory-burst] game-launch') mark('game-launch');
  if (message.text().startsWith('[memory-burst] asset:')) mark(message.text().slice(21));
  if (message.text().startsWith('[memory-burst] status:')) {
    const status = message.text().slice('[memory-burst] status:'.length);
    if (status !== lastStatus) {
      lastStatus = status;
      mark(`status:${status}`);
      if (status === 'Installing device...') mark('device-install');
      const phase = status === 'Installing device...' ? 'device-install-start' :
        status === 'Uploading SIS...' ? 'device-install-end' : null;
      if (phase) phaseCaptures.push(capturePhase(phase).catch(error =>
        marks.push({ms: performance.now(), phase, error: String(error)})));
    }
  }
});
const readMemory = async () => {
  const processes = [];
  for (const proc of (await system.send('SystemInfo.getProcessInfo')).processInfo) {
    try {
      const rollup = fs.readFileSync(`/proc/${proc.id}/smaps_rollup`, 'utf8');
      processes.push({pid: proc.id, type: proc.type,
        pssKiB: Number(rollup.match(/^Pss:\s+(\d+)/m)?.[1] || 0),
        rssKiB: Number(rollup.match(/^Rss:\s+(\d+)/m)?.[1] || 0)});
    } catch {}
  }
  return {ms: performance.now(), stage, processes,
    pssKiB: processes.reduce((sum, proc) => sum + proc.pssKiB, 0)};
};
const capturePhase = async phase => {
  const row = await readMemory();
  const renderer = row.processes.filter(proc => proc.type === 'renderer')
    .sort((left, right) => right.pssKiB - left.pssKiB)[0];
  if (renderer) fs.writeFileSync(path.join(output, `${phase}.smaps`),
    fs.readFileSync(`/proc/${renderer.pid}/smaps`));
  fs.writeFileSync(path.join(output, `${phase}.json`), JSON.stringify({
    ...row, wasmCapacity: await page.evaluate(() => HEAPU8.buffer.byteLength), renderer
  }));
};
const sample = async () => {
  if (sampling || closed) return;
  sampling = true;
  try {
    const row = await readMemory();
    samples.push(row);
    for (const anchor of anchors) {
      if (marks.some(mark => mark.name === `${anchor}-burst`)) continue;
      const start = marks.find(mark => mark.name === anchor);
      if (!start || row.ms < start.ms) continue;
      const eligible = sample => sample.ms <= row.ms - 200 && sample.ms >= start.ms;
      const prior = cumulative ? samples.find(eligible) : [...samples].reverse().find(eligible);
      if (!prior || row.pssKiB - prior.pssKiB < minimumKiB) continue;
      mark(`${anchor}-burst`);
      const renderer = row.processes.filter(proc => proc.type === 'renderer')
        .sort((left, right) => right.pssKiB - left.pssKiB)[0];
      const result = {anchor, from: prior, to: row};
      result.wasmCapacity = await page.evaluate(() => HEAPU8.buffer.byteLength);
      if (renderer) {
        result.renderer = renderer;
        fs.writeFileSync(path.join(output, `${anchor}-burst.smaps`),
          fs.readFileSync(`/proc/${renderer.pid}/smaps`));
      }
      const native = profiler?.targets.find(target => target.native);
      if (native) {
        try { result.native = (await native.client.send('Memory.getSamplingProfile')).profile; }
        catch (error) { result.nativeError = String(error); }
      }
      fs.writeFileSync(path.join(output, `${anchor}-burst.json`), JSON.stringify(result));
    }
  } finally { sampling = false; }
};
const timer = setInterval(() => { sample().catch(error => marks.push({ms: performance.now(), error: String(error)})); }, 100);
try {
  if (profiler && !manualLive) await profiler.start();
  await sample();
  await page.evaluateOnNewDocument(() => { window.ekaTransientSession = false; });
  if (livePath || manualLive) await page.evaluateOnNewDocument(() => {
    const timer = setInterval(() => {
      if (!window.Module?.setStatus) return;
      clearInterval(timer);
      const original = Module.setStatus;
      Module.setStatus = function(status) {
        if (status === 'Initializing...') console.log('[memory-burst] game-launch');
        console.log('[memory-burst] status:' + status);
        return original.apply(this, arguments);
      };
    }, 1);
  });
  mark('page-navigation');
  await page.goto(controlPage ? 'data:text/html,<title>Control</title>' :
    new URL(`?game=${livePath ? game : 'custom'}`, serverUrl).href,
    {waitUntil: 'domcontentloaded'});
  mark('dom-ready');
  if (controlPage) {
    await new Promise(resolve => setTimeout(resolve, 1500));
  } else {
    await page.waitForFunction(() => window.Module?.calledRun);
    mark('wasm-ready');
    if (profiler && manualLive) await profiler.start();
    if (livePath) {
      await page.waitForFunction(() => window._gameRunning ||
        document.getElementById('status')?.textContent.startsWith('Error:'));
      if (!await page.evaluate(() => window._gameRunning))
        throw Error(await page.$eval('#status', element => element.textContent));
    } else {
      const html = await page.evaluate(async () => await (await fetch('/')).text());
      const launch = html.match(/<script>configureGameLauncher\((\[.*\]), [^;]+\);<\/script>/);
      if (!launch) throw Error('Missing launcher configuration');
      const launchGame = JSON.parse(launch[1]).find(item => item.id === game);
      if (manualLive) {
        const cacheMode = process.env.EKA2L1_BURST_CACHE_MODE;
        if (cacheMode) await page.evaluate(mode => {
          window.ekaDownload = async endpoint => {
            const url = new URL(window.ekaAssetUrls[endpoint] || endpoint, location.href).href;
            if (mode === 'none') return fetch(url);
            if (mode !== 'cache-cycle' && mode !== 'clone') throw Error(`Unknown cache mode: ${mode}`);
            const cache = await caches.open('eka2l1-downloads-v1');
            const saved = await cache.match(url);
            if (saved) return saved;
            const response = await fetch(url);
            if (!response.ok || !window.ekaAssetUrls[endpoint]) return response;
            if (mode === 'clone') {
              await cache.put(url, response.clone());
              for (const key of await cache.keys())
                if (key.url !== url && new URL(key.url).pathname === new URL(url).pathname)
                  await cache.delete(key);
              return response;
            }
            await cache.put(url, response);
            return await cache.match(url);
          };
        }, cacheMode);
        mark('asset-load');
        await page.evaluate(async selected => {
          _selectedGame = selected;
          window.ekaRomCachePreparation = EkaRomCache.prepare(selected.romCache);
          const disk = await prepareDeviceStorage(selected);
          const useImage = selected.image && !disk?.manifest;
          const needsSis = !disk?.installed[assetHash(selected.sis)];
          const needsNgage = selected.ngage && !disk?.installed[assetHash(selected.ngage)];
          for (const [endpoint, selector, name] of (useImage ? [] : [
            ['/preload/rom', 'rom-file', 'SYM.ROM'],
            ['/preload/rpkg', 'rpkg-file', 'SYM.RPKG'],
            [selected.sis, 'sis-file', selected.id + '.sis']])) {
            if (!endpoint || (disk?.manifest && selector !== 'sis-file') ||
                (selector === 'sis-file' && !needsSis)) continue;
            console.log('[memory-burst] asset:fetch-start:' + endpoint);
            const response = await window.ekaDownload(endpoint);
            if (!response.ok) throw Error(`${endpoint}: HTTP ${response.status}`);
            const transfer = new DataTransfer();
            transfer.items.add(new File([await response.blob()], name));
            document.getElementById(selector).files = transfer.files;
            console.log('[memory-burst] asset:fetch-end:' + endpoint);
          }
          document.getElementById('app-name').value = selected.uid;
          window.measurementLaunch = startEmulator(useImage ? undefined : needsNgage ? selected.ngage : undefined,
            useImage ? selected.image : undefined);
        }, launchGame);
        await page.waitForFunction(() => window._gameRunning);
      } else {
        if (launchGame?.romCache) await page.evaluate(cache => EkaRomCache.prepare(cache), launchGame.romCache);
        mark('rom-cache-ready');
        const assetDir = process.env.EKA2L1_ASSET_DIR || '/tmp/eka2l1-serve';
        for (const [selector, file] of [['#rom-file', 'SYM.ROM'], ['#rpkg-file', 'SYM.RPKG'],
          ['#sis-file', entry.ngage?.runtime.file ?? entry.file]]) {
          await (await page.$(selector)).uploadFile(path.join(assetDir, file));
        }
        await page.$eval('#app-name', (element, value) => { element.value = value; }, entry.uid);
        mark('assets-selected');
        mark('game-launch');
        await page.evaluate(entryUrl => { window.measurementLaunch = startEmulator(entryUrl); },
          entry.ngage ? `/preload/games/${game}/ngage` : undefined);
        await page.waitForFunction(() => window._gameRunning);
      }
    }
    mark('game-running');
    await new Promise(resolve => setTimeout(resolve, 1000));
    const row = await readMemory();
    const renderer = row.processes.filter(proc => proc.type === 'renderer')
      .sort((left, right) => right.pssKiB - left.pssKiB)[0];
    if (renderer) fs.writeFileSync(path.join(output, 'game-running.smaps'),
      fs.readFileSync(`/proc/${renderer.pid}/smaps`));
    fs.writeFileSync(path.join(output, 'game-running.json'), JSON.stringify({
      ...row, wasmCapacity: await page.evaluate(() => HEAPU8.buffer.byteLength), renderer
    }));
  }
} finally {
  closed = true;
  clearInterval(timer);
  await Promise.all(phaseCaptures);
  while (sampling) await new Promise(resolve => setTimeout(resolve, 20));
  fs.writeFileSync(path.join(output, 'timeline.json'), JSON.stringify({marks, samples}));
  if (profiler) await profiler.stop().catch(() => {});
  await browser.close();
}
