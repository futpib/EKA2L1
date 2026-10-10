// Exercise the served launcher and both real games in a fresh browser.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import puppeteer from 'puppeteer';
import {PNG} from 'pngjs';

const [url, output, onlyGame] = process.argv.slice(2);
if (!url || !output || (onlyGame && !['snakes','sky-force'].includes(onlyGame))) throw Error('Usage: node game-picker.ts URL NEW_OUTPUT [snakes|sky-force]');
fs.mkdirSync(output);
const report: any = {url, started: new Date().toISOString(), games: [], checks: [], errors: []};
const save = () => fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
const log = fs.createWriteStream(path.join(output, 'browser.log'));
const browser = await puppeteer.launch({executablePath: '/usr/bin/chromium', headless: true,
  ignoreDefaultArgs: ['--mute-audio'], args: ['--no-sandbox', '--disable-dev-shm-usage',
    // Chromium 153's legacy CDP touch injector loses events after same-origin
    // navigation, including on blank pages. Use its OS-like gesture route.
    '--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan,SyntheticPointerActions', '--enable-gpu', '--ignore-gpu-blocklist']});
try {
  report.browser = await browser.version();
  report.gpu = (await (await browser.target().createCDPSession()).send('SystemInfo.getInfo')).gpu;
  const page = await browser.newPage();
  await page.setViewport({width: 900, height: 800, hasTouch: true});
  page.on('console', message => log.write(message.text() + '\n'));
  page.on('pageerror', error => report.errors.push(String(error)));
  page.on('response', response => { if (response.status() >= 400) report.errors.push(response.url() + ': ' + response.status()); });
  page.on('requestfailed', request => {
    // Navigation deliberately cancels the previous document's downloads.
    if (request.failure()?.errorText !== 'net::ERR_ABORTED') report.errors.push(request.url() + ': ' + request.failure()?.errorText);
  });
  const state = () => page.evaluate(() => {
    const g = window as any;
    return {guestUs: g.Module._eka2l1_guest_time_us(), frames: g.Module._eka2l1_presentations(),
      inputs: g.Module._eka2l1_input_consumed(), audio: g.EkaAudio.stats,
      audioDevice: {state: g.EkaAudio.context?.state, time: g.EkaAudio.context?.currentTime,
        received: g.EkaAudio.received, sink: JSON.parse(g.Module.ccall('eka2l1_audio_stats', 'string', [], []))}};
  });
  const waitGuest = (us: number) => page.waitForFunction(t => (window as any).Module._eka2l1_guest_time_us() >= t, {timeout: 180000}, us);
  const screenshot = async (name: string, minColors = 64) => {
    // Menu transitions can briefly fade to black. Require a nontrivial frame
    // within five seconds; persistent blank output remains a failure.
    for (let attempt=0;attempt<20;attempt++) {
      const bytes = await (await page.$('#canvas'))!.screenshot({path: path.join(output, name + '.png')});
      const png = PNG.sync.read(Buffer.from(bytes)), colors = new Set<number>();
      for (let i = 0; i < png.data.length; i += 4) colors.add(png.data.readUInt32LE(i));
      if (colors.size > minColors) return crypto.createHash('sha256').update(png.data).digest('hex');
      await new Promise(resolve=>setTimeout(resolve,250));
    }
    throw Error('Game display remains blank or trivial: '+name);
  };
  const openMenu = async () => {
    if (!await page.$eval('#session-panel',e=>(e as HTMLDialogElement).open)) await page.click('#btn-game-menu');
  };
  const closeMenu = () => page.click('#session-panel [data-close]');
  const choose = async (id: string) => {
    await openMenu();
    await page.select('#game-select', id);
    await Promise.all([page.waitForNavigation({waitUntil: 'domcontentloaded'}), page.click('#btn-play')]);
  };
  await page.goto(url, {waitUntil: 'domcontentloaded'});
  const catalogue = JSON.parse(fs.readFileSync(new URL('./games.json', import.meta.url), 'utf8'));
  assert.deepEqual(await page.$$eval('#game-select option', options => options.map(o => (o as HTMLOptionElement).value)), [...catalogue.map((game:any)=>game.id), 'custom']);
  for (const [id, uid] of [['sky-force', '0xa020d913'], ['snakes', '0x2000730f']]) {
    if (onlyGame && onlyGame!==id) continue;
    await page.setViewport({width:900,height:800,hasTouch:true});
    await choose(id);
    await page.waitForFunction(() => (window as any)._gameRunning, {timeout: 180000});
    await page.waitForFunction(() => JSON.parse((window as any).Module.ccall(
      'eka2l1_memory_impl_stats', 'string', [], [])).direct_rebuilds > 0, {timeout: 180000});
    const launch = await page.evaluate(() => {
      const g = window as any;
      return {app: (document.querySelector('#app-name') as HTMLInputElement).value,
        memory: JSON.parse(g.Module.ccall('eka2l1_memory_impl_stats', 'string', [], [])),
        secure: isSecureContext, isolated: crossOriginIsolated, policy: g.ekaCompilerPolicy, assets: g.ekaAssetUrls,
        sound: g.EkaAudio.context?.state ?? null, sources: g._inputSources.size,
        selectedFiles: ['rom-file', 'rpkg-file', 'sis-file'].map(id => (document.getElementById(id) as HTMLInputElement).files!.length)};
    });
    assert.equal(launch.app, uid); assert.ok(launch.secure && launch.isolated && launch.policy.applied);
    assert.equal(launch.memory.mode, 2);
    assert.ok(launch.memory.direct_rebuilds > 0, 'Default direct memory was not used');
    assert.equal(launch.sound, null); assert.equal(launch.sources, 0);
    assert.deepEqual(launch.selectedFiles, [0, 0, 0]);
    if (id === 'sky-force') {
      for (const us of [6000000, 10000000, 14000000, 18000000, 22000000]) {
        await waitGuest(us); await screenshot(`${id}-menu-${us}`, 4);
        await page.keyboard.press('Enter', {delay: 600});
      }
      await waitGuest(28000000);
    } else {
      for (let us = 2000000; us <= 20000000; us += 2000000) {
        await waitGuest(us); await page.keyboard.press('Enter', {delay: 100});
      }
      await waitGuest(23000000);
    }
    await openMenu();
    await page.click('#btn-sound');
    await page.waitForFunction(() => (window as any).EkaAudio.context?.state === 'running' && !(window as any).EkaAudio.muted);
    await closeMenu();
    const start = await state(), begin = performance.now();
    const first = await screenshot(id + '-gameplay-start');
    await page.keyboard.press('ArrowLeft', {delay: 600});
    await page.keyboard.press('ArrowRight', {delay: 600});
    await page.setViewport({width: 390, height: 844, hasTouch: true});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    await page.screenshot({path: path.join(output, id + '-mobile.png')});
    assert.equal(await page.evaluate(() => (window as any).ekaTouchControls.profile), id);
    // This route receives the complete active-contact set on each move; adding
    // or removing a contact does not end the other finger's gesture.
    const inputSession=await page.createCDPSession();
    await inputSession.send('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});
    assert.equal(await page.evaluate(()=>navigator.maxTouchPoints),5);
    await page.evaluate(() => {
      (window as any).touchTrace=[];
      for(const type of ['pointerdown','pointermove','pointerup','pointercancel','lostpointercapture','blur'])
        window.addEventListener(type,event=>{
          const trace=(window as any).touchTrace;
          if(trace.length<100)trace.push({type,target:(event.target as HTMLElement)?.id,pointer:(event as PointerEvent).pointerId,trusted:event.isTrusted});
        },true);
    });
    const touchPoint = await page.$eval('#movement-pad', element => {
      const r = element.getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2,id:1};
    });
    const actionPoint = await page.$eval('#primary-action', element => {
      const r = element.getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2,id:2};
    });
    await inputSession.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[touchPoint]});
    const moved = {...touchPoint,x:touchPoint.x-40,y:touchPoint.y+(id==='sky-force'?40:0)};
    await inputSession.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[moved]});
    await inputSession.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[moved,actionPoint]});
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    report.checks.push({game:id,check:'simultaneous touch movement and action',passed:await page.evaluate(() => {
      const held=new Set((window as any)._inputSources.values());return held.has(14)&&held.has(167);
    }),state:await page.evaluate(() => ({sources:[...(window as any)._inputSources],pointers:[...(window as any).ekaTouchControls.pointers],trace:(window as any).touchTrace,focus:document.hasFocus(),visibility:document.visibilityState,touches:navigator.maxTouchPoints}))});
    save();
    await page.waitForFunction(() => {
      const held = new Set((window as any)._inputSources.values()); return held.has(14) && held.has(167);
    });
    await new Promise(resolve => setTimeout(resolve,600));
    await page.screenshot({path:path.join(output,id+'-touch-held.png')});
    assert.ok(await page.evaluate(() => (window as any).touchTrace.some((event:any)=>event.type==='pointerdown'&&event.trusted)));
    await inputSession.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[moved]});
    await page.waitForFunction(() => {
      const held=new Set((window as any)._inputSources.values());return held.has(14)&&!held.has(167);
    });
    await inputSession.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
    await page.waitForFunction(() => (window as any)._inputSources.size===0);
    await inputSession.detach();
    await page.setViewport({width:844,height:390,hasTouch:true});
    await page.screenshot({path:path.join(output,id+'-landscape.png')});
    await openMenu();
    await page.click('#btn-touch-settings');
    await page.screenshot({path:path.join(output,id+'-settings.png')});
    await page.click('#touch-settings [data-close]');
    await page.setViewport({width: 900, height: 800, hasTouch: true});
    await page.focus('#canvas');
    for (let i = 1; i <= 3; i++) {
      await new Promise(resolve => setTimeout(resolve, 10000));
      await screenshot(`${id}-gameplay-${i}`);
    }
    const end = await state(), seconds = (performance.now() - begin) / 1000;
    const last = await screenshot(id + '-gameplay-end');
    await page.setViewport({width:390,height:844,hasTouch:true});
    await new Promise(resolve => setTimeout(resolve,250));
    await page.screenshot({path:path.join(output,id+'-mobile-gameplay.png')});
    await page.setViewport({width:844,height:390,hasTouch:true});
    await new Promise(resolve => setTimeout(resolve,250));
    await page.screenshot({path:path.join(output,id+'-landscape-gameplay.png')});
    assert.notEqual(first, last); assert.ok(end.frames > start.frames + 20);
    assert.ok(end.inputs >= start.inputs + 6);
    // Preserve audio failures while exercising the other game and launcher.
    // The final assertion still fails the complete integration check.
    report.checks.push({game: id, check: 'non-silent browser audio', passed: end.audio.nonzero > 0});
    await openMenu();
    await page.click('#btn-sound');
    await page.waitForFunction(() => !(window as any).EkaAudio.busy, {timeout: 5000}).catch(() => {});
    report.checks.push({game: id, check: 'sound toggle returns to muted',
      passed: await page.evaluate(() => (window as any).EkaAudio.muted)});
    // Focused launcher arrows must stay in the selector, not move the game.
    await page.focus('#game-select');
    const before = (await state()).inputs;
    await page.keyboard.press('ArrowDown');
    assert.equal((await state()).inputs, before);
    const game = {id, launch, start, end, hostSeconds: seconds,
      realtimeRatio: (end.guestUs - start.guestUs) / 1e6 / seconds,
      presentationsPerSecond: (end.frames - start.frames) / seconds};
    report.games.push(game); save(); console.log(JSON.stringify(game));
  }
  await choose('custom');
  await page.waitForFunction(() => (window as any).Module.calledRun);
  assert.equal(await page.$eval('#controls', element => (element as HTMLElement).hidden), false);
  assert.equal(await page.evaluate(() => (window as any)._gameRunning), false);
  assert.equal(await page.evaluate(() => (window as any).EkaAudio.context), null);
  assert.deepEqual(report.errors, []);
  report.passed = report.checks.every((check: any) => check.passed); save();
  assert.ok(report.passed, 'Game checks failed; see the retained report for both games');
  console.log('PASS: Sky Force and Snakes, switching, keyboard/touch, audio, mobile layout and manual launcher');
} finally { save(); await browser.close(); log.end(); }
