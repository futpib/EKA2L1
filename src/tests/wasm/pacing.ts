// Exercise the normal launcher across real renderer stalls, not a mocked clock.
import fs from 'node:fs';
import path from 'node:path';
import puppeteer from 'puppeteer';
import {startServer, compilerPolicyFromEnv} from './server.ts';

const [assets, output, existingUrl] = process.argv.slice(2);
if (!assets || !output || process.platform !== 'linux')
  throw Error('Usage (Linux): node pacing.ts ASSETS NEW_OUTPUT [EXISTING_URL]');
fs.mkdirSync(output);
const policy = compilerPolicyFromEnv();
const local = existingUrl ? null : await startServer(0, {
  '/preload/rom': path.join(assets, 'SYM.ROM'),
  '/preload/rpkg': path.join(assets, 'SYM.RPKG'),
  '/preload/sis': path.join(assets, 'Snakes.sis'),
}, 'Snakes', {compilerPolicy: policy});
const url = existingUrl || `http://127.0.0.1:${local!.port}/`;
const report: any = {url, started: new Date().toISOString(), phases: [], errors: []};
const save = () => fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
const log = fs.createWriteStream(path.join(output, 'browser.log'));
const stopped = new Set<number>();
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
let browser: Awaited<ReturnType<typeof puppeteer.launch>> | undefined;

try {
  browser = await puppeteer.launch({executablePath: '/usr/bin/chromium', headless: true,
    ignoreDefaultArgs: ['--mute-audio'], args: ['--no-sandbox', '--disable-dev-shm-usage',
      '--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist']});
  report.browser = await browser.version();
  const system = await browser.target().createCDPSession();
  report.gpu = (await system.send('SystemInfo.getInfo')).gpu;
  const page = await browser.newPage();
  await page.setViewport({width: 900, height: 760});
  page.on('pageerror', error => report.errors.push(String(error)));
  page.on('console', message => log.write(message.type() + ': ' + message.text() + '\n'));
  page.on('requestfailed', request => report.errors.push(request.url() + ': ' + request.failure()?.errorText));
  page.on('response', response => {if (response.status() >= 400) report.errors.push(response.url() + ': ' + response.status());});
  await page.goto(url, {waitUntil: 'domcontentloaded'});
  await page.waitForFunction(() => (window as any)._gameRunning, {timeout: 180000});
  report.launch = await page.evaluate(() => {
    const g = window as any;
    return {policy: g.ekaCompilerPolicy, urls: g.ekaAssetUrls,
      unsafeCode: g.Module._eka2l1_unsafe_code_report(), secure: isSecureContext, isolated: crossOriginIsolated};
  });
  if (!report.launch.policy?.applied || !report.launch.secure || !report.launch.isolated)
    throw Error('Launcher, security or compiler policy not active');
  if (JSON.stringify(report.launch.policy.requested) !== JSON.stringify(policy))
    throw Error('Unexpected launcher policy');
  for (let guest = 2000000; guest <= 20000000; guest += 2000000) {
    await page.waitForFunction(t => (window as any).Module._eka2l1_guest_time_us() >= t, {timeout: 120000}, guest);
    await page.keyboard.press('Enter', {delay: 50});
  }
  await page.waitForFunction(() => (window as any).Module._eka2l1_guest_time_us() >= 23000000, {timeout: 120000});
  await page.click('#btn-sound');
  await page.waitForFunction(() => (window as any).EkaAudio.context?.state === 'running');
  const zero = performance.now();
  const sample = async () => {
    const begin = performance.now();
    const state = await page.evaluate(() => {
      const g = window as any;
      return {guestUs: g.Module._eka2l1_guest_time_us(), frames: g.Module._eka2l1_presentations(),
        inputs: g.Module._eka2l1_input_consumed(), audio: g.EkaAudio.stats};
    });
    const end = performance.now();
    return {hostSeconds: ((begin + end) / 2 - zero) / 1000, queryMs: end - begin, ...state};
  };
  const summarize = (samples: any[]) => {
    const a = samples[0], b = samples.at(-1), seconds = b.hostSeconds - a.hostSeconds;
    return {hostSeconds: seconds, guestSeconds: (b.guestUs - a.guestUs) / 1e6,
      ratio: (b.guestUs - a.guestUs) / 1e6 / seconds, framesPerSecond: (b.frames - a.frames) / seconds};
  };
  const measure = async (name: string, seconds: number) => {
    const phase: any = {name, samples: [await sample()]};
    report.phases.push(phase);
    const begin = performance.now();
    while (performance.now() - begin < seconds * 1000) {
      await sleep(100);
      phase.samples.push(await sample());
    }
    phase.summary = summarize(phase.samples);
    phase.windows = [];
    for (let i = 0; i < phase.samples.length; i++) {
      const start = phase.samples[i];
      const end = phase.samples.findIndex((s: any, j: number) => j > i && s.hostSeconds - start.hostSeconds >= 1);
      if (end < 0) break;
      phase.windows.push(summarize(phase.samples.slice(i, end + 1)));
    }
    await (await page.$('#canvas'))!.screenshot({path: path.join(output, name + '.png')});
    save();
    console.log(name + ': ' + JSON.stringify(phase.summary));
    // Bound every one-second interval as well as the average: a whole-run
    // average alone can hide a stall followed by exactly compensating overspeed.
    if (phase.summary.ratio > 1.02 || phase.windows.some((w: any) => w.ratio > 1.05))
      throw Error(name + ': guest fast-forwarded after a host stall');
    if (phase.summary.ratio < 0.98 || phase.summary.framesPerSecond < 18)
      throw Error(name + ': host did not sustain real-time gameplay');
    return phase;
  };
  await measure('fresh', 8);
  for (const ms of [250, 6000]) {
    const pids = (await system.send('SystemInfo.getProcessInfo')).processInfo
      .filter(process => process.type === 'renderer').map(process => process.id);
    if (!pids.length) throw Error('No test-browser renderer to suspend');
    const before = await sample();
    const phase: any = {name: 'stall-' + ms, pids, before};
    report.phases.push(phase);
    const begin = performance.now();
    // CDP lists only this owned browser's processes. Always resume them before
    // cleanup, including when measurement or an assertion fails.
    for (const pid of pids) {process.kill(pid, 'SIGSTOP'); stopped.add(pid);}
    try {await sleep(ms);} finally {
      for (const pid of stopped) process.kill(pid, 'SIGCONT');
      stopped.clear();
    }
    phase.actualHostSeconds = (performance.now() - begin) / 1000;
    phase.after = await sample();
    save();
    await measure('recovery-' + ms, 10);
    const previous = await sample();
    await page.keyboard.press('ArrowRight', {delay: 70});
    await sleep(100);
    if ((await sample()).inputs <= previous.inputs) throw Error('Input failed after renderer resume');
  }
  // A renderer stall also stalls the audio worklet. Judge uninterrupted audio
  // only after its ordinary recovery rather than treating that forced gap as a bug.
  const settled = await measure('settled', 8);
  const first = settled.samples[0].audio, last = settled.samples.at(-1).audio;
  if (!last.nonzero || last.played <= first.played || last.dropped !== first.dropped || last.underruns !== first.underruns)
    throw Error('Audio did not settle after renderer resume');
  if (report.errors.length) throw Error(report.errors.join('\n'));
  report.passed = true;
  save();
  console.log('PASS: normal gameplay and bounded recovery after short/long renderer stalls');
} catch (error) {
  report.errors.push(String(error)); save(); throw error;
} finally {
  for (const pid of stopped) {try {process.kill(pid, 'SIGCONT');} catch {}}
  await browser?.close(); local?.server.close(); log.end();
}
