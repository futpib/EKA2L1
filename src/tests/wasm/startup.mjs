// Fresh browser/cache and guest filesystem through the normal paced launcher.
// First presentation can be a logo or the N-Gage client, not a playable game.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import puppeteer from 'puppeteer';
import {PNG} from 'pngjs';
import {sampleCpuTime, cpuTimeDelta} from './cpu-time.ts';

const [baseUrl, output, game, limit = '240'] = process.argv.slice(2);
const games = JSON.parse(fs.readFileSync(new URL('./games.json', import.meta.url)));
const entry = games.find(g => g.id === game);
if (!baseUrl || !output || !entry || !/^\d+$/.test(limit) || Number(limit) < 15)
  throw Error('Usage: node startup.mjs URL NEW_OUTPUT GAME [TIMEOUT_SECONDS]');
fs.mkdirSync(output);
const file = name => path.join(output, name);
const report = {game, url:baseUrl, started:new Date().toISOString(),
  mode:'normal paced launcher; fresh browser, asset cache and temporary guest filesystem',
  cpuModel:os.cpus()[0]?.model, loadBefore:os.loadavg(),
  requiresSceneReview:true, milestones:[], samples:[], errors:[]};
const save = () => fs.writeFileSync(file('report.json'), JSON.stringify(report, null, 2));
const browser = await puppeteer.launch({executablePath:'/usr/bin/chromium', headless:true,
  args:['--host-resolver-rules=MAP claude-laptop.lan 127.0.0.1',
    '--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=vulkan',
    '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist',
    '--disable-background-timer-throttling']});
try {
  report.browser = await browser.version();
  const root = await browser.target().createCDPSession();
  report.gpu = (await root.send('SystemInfo.getInfo')).gpu;
  const page = await browser.newPage();
  await page.setViewport({width:900, height:800});
  page.setDefaultTimeout(Number(limit) * 1000);
  await page.evaluateOnNewDocument(() => {
    window.ekaTransientSession = true;
    window.startupProbe = {runtime:null, running:null, first:null};
    const tick = () => {
      const p = window.startupProbe, m = window.Module;
      if (m?.calledRun) {
        p.runtime ??= performance.now();
        if (window._gameRunning) p.running ??= performance.now();
        if (m._eka2l1_presentations?.() > 0) p.first ??= {
          ms:performance.now(), frames:m._eka2l1_presentations(),
          monitor:JSON.parse(m.ccall('eka2l1_monitor_report', 'string', [], [])),
        };
      }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  });
  let begin = performance.now();
  page.on('console', message => {
    const ms = performance.now() - begin, text = message.text();
    fs.appendFileSync(file('browser.log'), `${ms.toFixed(2)} ${text}\n`);
    if (text.startsWith('[status]') || /precompiled|Spawned process|Emulator thread started|AOT: instantiated/.test(text))
      report.milestones.push({ms, text});
    // Package deletion precedes extraction completion and is not a ready signal.
    if (text.includes('Thread N-Gage Installer forcefully killed with category: None and exit code: 0'))
      report.installationComplete = {ms, text};
    if (text.startsWith('[installed-image]')) report.preinstalled = {ms, text};
    if (/Access violation|KERN-EXEC|ABORT:|panicked with category:/.test(text)) report.errors.push(text);
  });
  page.on('pageerror', e => report.errors.push(String(e)));
  const url = new URL(baseUrl);
  url.searchParams.set('game', game);
  begin = performance.now();
  await page.goto(url.href, {waitUntil:'domcontentloaded'});
  await page.waitForFunction(() => window._gameRunning && window.startupProbe.first);
  report.first = await page.evaluate(() => window.startupProbe);
  const cpuBegin = await sampleCpuTime(root);
  const deadline = begin + Number(limit) * 1000;
  for (let index = 0; performance.now() < deadline; ++index) {
    const sample = await page.evaluate(() => ({ms:performance.now(),
      guest:Module._eka2l1_guest_time_us(), frames:Module._eka2l1_presentations(),
      monitor:JSON.parse(Module.ccall('eka2l1_monitor_report', 'string', [], [])),
      status:document.querySelector('#status').textContent}));
    sample.screenshot = `screen-${index}.png`;
    // A canvas bounding-box screenshot also includes HTML layered above it.
    // Exclude controls/FPS so they cannot make a black guest frame nontrivial.
    const overlays = await page.evaluate(() => Array.from(document.querySelector('#play-surface').children)
      .filter(element => element.id !== 'canvas-container').map(element => {
        const previous = element.style.visibility;
        element.style.visibility = 'hidden';
        return [element.id, previous];
      }));
    let bytes;
    try {
      bytes = await (await page.$('#canvas')).screenshot({path:file(sample.screenshot)});
    } finally {
      await page.evaluate(previous => {
        for (const [id, visibility] of previous) document.getElementById(id).style.visibility = visibility;
      }, overlays);
    }
    const png = PNG.sync.read(Buffer.from(bytes)), colors = new Set();
    for (let i = 0; i < png.data.length; i += 4) colors.add(png.data.readUInt32LE(i));
    sample.colors = colors.size;
    report.samples.push(sample);
    if (colors.size > 64) report.nontrivial ??= sample;
    save();
    console.log(game, (sample.ms / 1000).toFixed(2), sample.frames, sample.colors);
    if (entry.ngage && !report.preinstalled ? report.installationComplete && performance.now() - begin - report.installationComplete.ms > 1500
      : report.nontrivial && sample.ms - report.nontrivial.ms > 6000) break;
    await new Promise(resolve => setTimeout(resolve, 2000));
  }
  report.cpuAfterFirstPresentation = cpuTimeDelta(cpuBegin, await sampleCpuTime(root));
  report.final = await page.evaluate(() => ({probe:window.startupProbe,
    resources:performance.getEntriesByType('resource').map(x => ({
      name:x.name, start:x.startTime, end:x.responseEnd, transfer:x.transferSize})),
    assets:window.ekaAssetUrls, policy:window.ekaCompilerPolicy}));
  if (entry.ngage && !report.preinstalled && !report.installationComplete) throw Error('Installer did not complete before timeout');
  if ((!entry.ngage || report.preinstalled) && !report.nontrivial) throw Error('No nontrivial guest image before timeout');
  if (report.errors.length) throw Error('Browser or guest error; inspect report.json');
} catch (e) {
  report.failure = String(e);
  throw e;
} finally {
  save();
  await browser.close();
}
