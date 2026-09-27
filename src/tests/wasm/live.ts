import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import puppeteer from 'puppeteer';
import {PNG} from 'pngjs';
import {startServer, buildDir} from './server.ts';

const [assetArg, outputArg, durationArg = '60'] = process.argv.slice(2);
if (!assetArg || !outputArg) throw new Error('Usage: node live.ts ASSETS NEW_OUTPUT [PLAY_SECONDS]');
const duration = Number(durationArg);
if (!Number.isFinite(duration) || duration < 5 || duration > 300) throw new Error('Invalid duration');
const profileStart = Number(process.env.EKA2L1_LIVE_PROFILE_START_US || 0);
const profileEnd = Number(process.env.EKA2L1_LIVE_PROFILE_END_US || 0);
if (profileStart && (!Number.isSafeInteger(profileStart) || !Number.isSafeInteger(profileEnd) || profileEnd <= profileStart)) throw new Error('Invalid live profile window');
const assets = path.resolve(assetArg), output = path.resolve(outputArg);
fs.mkdirSync(output);
const {server, port} = await startServer(0);
const browser = await puppeteer.launch({executablePath: '/usr/bin/chromium', headless: true,
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
const state = () => page.evaluate(() => ({guest: (window as any).Module._eka2l1_guest_time_us(),
  inputs: (window as any).Module._eka2l1_input_consumed(), frames: (window as any).Module._eka2l1_presentations()}));
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
  for (const [selector, file] of [['#rom-file','SYM.ROM'],['#rpkg-file','SYM.RPKG'],['#sis-file','Snakes.sis']])
    await (await page.$(selector))!.uploadFile(path.join(assets,file));
  await page.type('#app-name','Snakes');
  await page.click('#btn-start');
  await page.waitForFunction(() => (window as any)._gameRunning, {timeout:120000});
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
    samples.push({host_seconds:(performance.now()-hostStart)/1000, ...current});
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
  await page.evaluate(() => { (window as any)._gameRunning = false; (window as any).Module._eka2l1_shutdown(); });
  if (errors.length) throw new Error(errors.join('\n'));
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({start,end,host_seconds:elapsed,
    sampling:!!profileStart, profile_window:{begin:profileBegin,end:profileFinish},
    measurement:profileBegin ? {first_virtual_us:profileBegin.guest,last_virtual_us:profileFinish.guest,wall_seconds:profileFinish.host_seconds-profileBegin.host_seconds} : null,
    renderer:'See gpu.json for physical GPU details',
    realtime_ratio:(end.guest-start.guest)/1e6/elapsed, input_delivery_ms:latencies, samples,
    browser:await browser.version(), wasm_sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(buildDir,'eka2l1.wasm'))).digest('hex'),
    note:'Input latency measures DOM keydown to guest queue consumption, not display response.'},null,2));
  console.log('PASS: live UI, keyboard/touch, pacing measurement and shutdown');
} finally { await browser.close(); server.close(); log.end(); }
