// Exercise the served launcher and both real games in a fresh browser.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import puppeteer from 'puppeteer';
import {PNG} from 'pngjs';

const [url, output] = process.argv.slice(2);
if (!url || !output) throw Error('Usage: node game-picker.ts URL NEW_OUTPUT');
fs.mkdirSync(output);
const report: any = {url, started: new Date().toISOString(), games: [], checks: [], errors: []};
const save = () => fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
const log = fs.createWriteStream(path.join(output, 'browser.log'));
const browser = await puppeteer.launch({executablePath: '/usr/bin/chromium', headless: true,
  ignoreDefaultArgs: ['--mute-audio'], args: ['--no-sandbox', '--disable-dev-shm-usage',
    '--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist']});
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
    const bytes = await (await page.$('#canvas'))!.screenshot({path: path.join(output, name + '.png')});
    const png = PNG.sync.read(Buffer.from(bytes)), colors = new Set<number>();
    for (let i = 0; i < png.data.length; i += 4) colors.add(png.data.readUInt32LE(i));
    assert.ok(colors.size > minColors, 'Game display is blank or trivial');
    return crypto.createHash('sha256').update(png.data).digest('hex');
  };
  const choose = async (id: string) => {
    await page.select('#game-select', id);
    await Promise.all([page.waitForNavigation({waitUntil: 'domcontentloaded'}), page.click('#btn-play')]);
  };
  await page.goto(url, {waitUntil: 'domcontentloaded'});
  assert.deepEqual(await page.$$eval('#game-select option', options => options.map(o => (o as HTMLOptionElement).value)), ['snakes', 'sky-force', 'custom']);
  for (const [id, uid] of [['sky-force', '0xa020d913'], ['snakes', '0x2000730f']]) {
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
    await page.click('#btn-sound');
    await page.waitForFunction(() => (window as any).EkaAudio.context?.state === 'running' && !(window as any).EkaAudio.muted);
    const start = await state(), begin = performance.now();
    const first = await screenshot(id + '-gameplay-start');
    await page.keyboard.press('ArrowLeft', {delay: 600});
    await page.keyboard.press('ArrowRight', {delay: 600});
    await page.setViewport({width: 390, height: 844, hasTouch: true});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    await page.screenshot({path: path.join(output, id + '-mobile.png')});
    await (await page.$('[data-scan="14"]'))!.tap();
    await page.setViewport({width: 900, height: 800, hasTouch: true});
    await page.focus('#canvas');
    for (let i = 1; i <= 3; i++) {
      await new Promise(resolve => setTimeout(resolve, 10000));
      await screenshot(`${id}-gameplay-${i}`);
    }
    const end = await state(), seconds = (performance.now() - begin) / 1000;
    const last = await screenshot(id + '-gameplay-end');
    assert.notEqual(first, last); assert.ok(end.frames > start.frames + 20);
    assert.ok(end.inputs >= start.inputs + 6);
    // Preserve audio failures while exercising the other game and launcher.
    // The final assertion still fails the complete integration check.
    report.checks.push({game: id, check: 'non-silent browser audio', passed: end.audio.nonzero > 0});
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
