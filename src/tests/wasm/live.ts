import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import puppeteer from 'puppeteer';
import {PNG} from 'pngjs';
import {startServer, buildDir, compilerPolicyFromEnv} from './server.ts';

const [assetArg, outputArg, durationArg = '60'] = process.argv.slice(2);
if (!assetArg || !outputArg) throw new Error('Usage: node live.ts ASSETS NEW_OUTPUT [PLAY_SECONDS]');
const duration = Number(durationArg);
if (!Number.isFinite(duration) || duration < 5 || duration > 300) throw new Error('Invalid duration');
const profileStart = Number(process.env.EKA2L1_LIVE_PROFILE_START_US || 0);
const profileEnd = Number(process.env.EKA2L1_LIVE_PROFILE_END_US || 0);
if (profileStart && (!Number.isSafeInteger(profileStart) || !Number.isSafeInteger(profileEnd) || profileEnd <= profileStart)) throw new Error('Invalid live profile window');
const assets = path.resolve(assetArg), output = path.resolve(outputArg);
fs.mkdirSync(output);
const audioEnabled = process.env.EKA2L1_LIVE_AUDIO === '1';
const autoStart = process.env.EKA2L1_LIVE_AUTOSTART === '1';
const preloads: Record<string,string> = autoStart ? {'/preload/rom':path.join(assets,'SYM.ROM'),
  '/preload/rpkg':path.join(assets,'SYM.RPKG'),'/preload/sis':path.join(assets,'Snakes.sis')} : {};
const compilerPolicy = compilerPolicyFromEnv();
const {server, port} = await startServer(0, preloads, autoStart ? 'Snakes' : undefined, {compilerPolicy});
const browser = await puppeteer.launch({executablePath: '/usr/bin/chromium', headless: true, ignoreDefaultArgs: audioEnabled ? ['--mute-audio'] : [],
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=vulkan',
    '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist', '--disable-background-timer-throttling']});
const system = await browser.target().createCDPSession();
fs.writeFileSync(path.join(output, 'gpu.json'), JSON.stringify((await system.send('SystemInfo.getInfo')).gpu, null, 2));
const page = await browser.newPage();
await page.setViewport({width: 900, height: 760, hasTouch:true});
const errors: string[] = [], latencies: number[] = [];
const log = fs.createWriteStream(path.join(output, 'browser.log'));
page.on('console', m => { log.write(`${m.type()}: ${m.text()}\n`); if (m.text().includes('ABORT:')) errors.push(m.text()); });
page.on('pageerror', e => errors.push(String(e)));
page.on('requestfailed', r => errors.push(`${r.url()}: ${r.failure()?.errorText}`));
page.on('response', r => {if (r.status() >= 400) errors.push(`HTTP ${r.status()} ${r.url()}`);});
const pause = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
const audioState = () => page.evaluate(() => {
  const g=window as any, a=g.EkaAudio;
  return {context_time:a.context?.currentTime, state:a.context?.state, worklet:a.stats,
    received:a.received, sink:JSON.parse(g.Module.ccall('eka2l1_audio_stats','string',[],[]))};
});
const state = () => page.evaluate(() => ({guest: (window as any).Module._eka2l1_guest_time_us(),
  inputs: (window as any).Module._eka2l1_input_consumed(), frames: (window as any).Module._eka2l1_presentations()}));
async function resources() {
  const snapshot = await page.evaluate(() => {
    const g = window as any;
    const tmp = g.FS.readdir('/tmp').filter((n:string) => n !== '.' && n !== '..')
      .map((n:string) => ({name:n, bytes:g.FS.stat('/tmp/'+n).size}));
    return {tmp, worker_pool:{running:g.PThread.runningWorkers.length,unused:g.PThread.unusedWorkers.length},
      selected_files:['rom-file','rpkg-file','sis-file'].map(id => (document.getElementById(id) as HTMLInputElement).files!.length),
      linear_bytes:g.HEAPU8.buffer.byteLength,
      allocator:JSON.parse(g.Module.ccall('eka2l1_monitor_report','string',[],[]))};
  });
  let pssKiB = 0;
  for (const proc of (await system.send('SystemInfo.getProcessInfo')).processInfo) {
    try { pssKiB += Number(fs.readFileSync(`/proc/${proc.id}/smaps_rollup`,'utf8').match(/^Pss:\s+(\d+)/m)?.[1] ?? 0); } catch {}
  }
  return {...snapshot,pss_kib:pssKiB};
}
async function waitGuest(us: number) {
  const deadline = performance.now() + 120000;
  while ((await state()).guest < us) {
    if (errors.length) throw new Error(errors.join('\n'));
    if (performance.now() > deadline) throw new Error('Guest progress timeout');
    await pause(50);
  }
}
async function key(key: string, hold = 70) {
  const previous = (await state()).inputs;
  const begin = performance.now();
  await page.keyboard.down(key);
  while ((await state()).inputs <= previous) {
    if (performance.now() - begin > 2000) throw new Error('Keyboard input not consumed');
    await pause(2);
  }
  latencies.push(performance.now() - begin);
  await pause(hold);
  await page.keyboard.up(key);
}
async function visible(name: string, requireContent = true) {
  const bytes = Buffer.from(await (await page.$('#canvas'))!.screenshot());
  fs.writeFileSync(path.join(output, `${name}.png`), bytes);
  const pixels = PNG.sync.read(bytes).data;
  const colors = new Set<number>();
  for (let i = 0; i < pixels.length; i += 4) colors.add((pixels[i]<<16)|(pixels[i+1]<<8)|pixels[i+2]);
  if (requireContent && colors.size < 4) throw new Error('Trivial visible canvas');
  return crypto.createHash('sha256').update(pixels).digest('hex');
}
try {
  await page.goto(`http://127.0.0.1:${port}/`, {waitUntil:'domcontentloaded'});
  await page.waitForFunction(() => (window as any).Module?.calledRun);
  if (!autoStart) {
    for (const [selector, file] of [['#rom-file','SYM.ROM'],['#rpkg-file','SYM.RPKG'],['#sis-file','Snakes.sis']])
      await (await page.$(selector))!.uploadFile(path.join(assets,file));
    await page.type('#app-name','Snakes');
    await page.click('#btn-start');
  }
  await page.waitForFunction(() => (window as any)._gameRunning, {timeout:120000});
  const appliedPolicy = await page.evaluate(() => (window as any).ekaCompilerPolicy ?? null);
  if (compilerPolicy && (!appliedPolicy?.applied || JSON.stringify(appliedPolicy.requested) !== JSON.stringify(compilerPolicy)))
    throw new Error('Live run did not apply the requested compiler policy');
  const unsafeCode = await page.evaluate(() => (window as any).Module._eka2l1_unsafe_code_report());
  if (unsafeCode !== compilerPolicy.unsafeCode) throw Error('Live executable-byte mode mismatch');
  if (audioEnabled) {
    await page.click('#btn-sound');
    await page.waitForFunction(() => (window as any).EkaAudio.context?.state === 'running' && !(window as any).EkaAudio.muted);
    if (!await page.evaluate(() => document.activeElement?.id === 'canvas')) throw Error('Audio control trapped game keyboard focus');
  }
  const startupResources = await resources();
  if (process.env.EKA2L1_EXPECT_UPLOAD_RELEASE === '1' &&
      (startupResources.selected_files.some(n => n !== 0) ||
       startupResources.tmp.some(f => /\.(rom|rpkg|sis)$/i.test(f.name))))
    throw new Error('Installation retained upload files');
  for (let us=2000000; us<=20000000; us+=2000000) { await waitGuest(us); await key('Enter',50); }
  await waitGuest(23000000);
  const firstImage = await visible('desktop-gameplay');
  await key('ArrowRight');
  await key('ArrowUp');
  await page.setViewport({width:390,height:844,hasTouch:true});
  await page.screenshot({path:path.join(output,'mobile.png')});
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
  if (overflow) throw new Error('Mobile horizontal overflow');
  const previous = (await state()).inputs;
  await (await page.$('[data-scan="14"]'))!.tap();
  await pause(100);
  if ((await state()).inputs < previous + 2) throw new Error('Touch press/release not consumed');
  await page.setViewport({width:900,height:760,hasTouch:true});
  await page.focus('#canvas');
  const audioStart = audioEnabled ? await audioState() : null;
  const start = await state(), hostStart = performance.now(), samples = [];
  let profileClients: {name:string, client:any}[] = [], profileBegin:any = null, profileFinish:any = null;
  const stopProfile = async (current:any) => {
    await Promise.all(profileClients.map(async ({name,client}) => {
      const {profile} = await client.send('Profiler.stop');
      fs.writeFileSync(path.join(output,`${name}.cpuprofile`),JSON.stringify(profile));
    }));
    profileFinish = {host_seconds:(performance.now()-hostStart)/1000,...current};
  };
  while (performance.now() - hostStart < duration*1000) {
    await pause(1000);
    const current = await state();
    if (profileStart && !profileBegin && current.guest >= profileStart) {
      profileClients = [{name:'page',client:await page.createCDPSession()},
        ...page.workers().map((worker,i) => ({name:`worker-${i}`,client:(worker as any).client}))];
      await Promise.all(profileClients.map(async ({client}) => {
        await client.send('Profiler.enable'); await client.send('Profiler.setSamplingInterval',{interval:1000}); await client.send('Profiler.start');
      }));
      profileBegin = {host_seconds:(performance.now()-hostStart)/1000,...current};
    }
    if (profileBegin && !profileFinish && current.guest >= profileEnd) await stopProfile(current);
    samples.push({host_seconds:(performance.now()-hostStart)/1000, ...current, ...(audioEnabled ? {audio:await audioState()} : {})});
    // Retain scene evidence throughout long trials so a fast menu cannot be
    // mistaken for sustained gameplay. Screenshot cost stays in elapsed time.
    if (samples.length % 20 === 0) await visible(`gameplay-${samples.length}`, false);
    if (errors.length) throw new Error(errors.join('\n'));
  }
  const end = await state(), elapsed = (performance.now()-hostStart)/1000;
  if (profileBegin && !profileFinish) await stopProfile(end);
  const lastImage = await visible('desktop-end');
  if (firstImage === lastImage) throw new Error('Game display stopped changing');
  const consumedBefore = end.inputs;
  await page.keyboard.down('ArrowLeft');
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await pause(100);
  if ((await state()).inputs < consumedBefore+2) throw new Error('Blur failed to release held input');
  await page.keyboard.up('ArrowLeft');
  const audio = audioEnabled ? await page.evaluate(() => {
    const g = window as any, a = g.EkaAudio;
    return {state:a.context.state,rate:a.context.sampleRate,muted:a.muted,received:a.received,worklet:a.stats,
      sink:JSON.parse(g.Module.ccall('eka2l1_audio_stats','string',[],[]))};
  }) : null;
  if (audioEnabled) {
    if (!audio.worklet.nonzero || audio.worklet.played < duration*40000 || audio.state !== 'running') throw Error('Audio did not play');
    await page.click('#btn-sound');
    await pause(20);
    if (!await page.evaluate(() => (window as any).EkaAudio.muted && (window as any).EkaAudio.gain.gain.value === 0)) throw Error('Mute failed');
    await page.click('#btn-sound');
    await pause(20);
    if (!await page.evaluate(() => !(window as any).EkaAudio.muted && (window as any).EkaAudio.gain.gain.value === 1)) throw Error('Unmute failed');
  }
  const audioContinuity = audioEnabled ? {
    added_underruns:audio.worklet.underruns-audioStart.worklet.underruns,
    added_drops:audio.worklet.dropped-audioStart.worklet.dropped,
    max_sampled_queue:Math.max(...samples.map(s=>s.audio.worklet.queued)),
    device_seconds:(await audioState()).context_time-audioStart.context_time
  } : null;
  const finalResources = await resources();
  await page.evaluate(() => { (window as any)._gameRunning = false; (window as any).Module._eka2l1_shutdown(); });
  if (audioEnabled && !await page.evaluate(() => !(window as any).EkaAudio.context && !(window as any).EkaAudio.timer)) throw Error('Audio shutdown failed');
  if (errors.length) throw new Error(errors.join('\n'));
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({start,end,host_seconds:elapsed,audio,audio_continuity:audioContinuity,audio_start:audioStart,
    resources:{startup:startupResources,final:finalResources}, compiler_policy:appliedPolicy, unsafe_code:unsafeCode, auto_start:autoStart, sampling:!!profileStart, profile_window:{begin:profileBegin,end:profileFinish},
    measurement:profileBegin ? {first_virtual_us:profileBegin.guest,last_virtual_us:profileFinish.guest,wall_seconds:profileFinish.host_seconds-profileBegin.host_seconds} : null,
    renderer:'See gpu.json for physical GPU details',
    realtime_ratio:(end.guest-start.guest)/1e6/elapsed, input_delivery_ms:latencies, samples,
    browser:await browser.version(), wasm_sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(buildDir,'eka2l1.wasm'))).digest('hex'),
    note:'Input latency measures DOM keydown to guest queue consumption, not display response.'},null,2));
  if (audioEnabled && (audioContinuity.added_underruns || audioContinuity.added_drops)) throw Error('Audio interrupted during measured gameplay; see report.json');
  console.log('PASS: live UI, keyboard/touch, pacing measurement and shutdown');
} finally { await browser.close(); server.close(); log.end(); }
