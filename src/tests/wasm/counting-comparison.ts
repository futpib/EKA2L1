// One fresh, paced browser session. Inspect the saved scenes before accepting
// a result: animated menus also produce presentations.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import puppeteer from 'puppeteer';

const [baseUrl, output, family, game, counting, duration = '30'] = process.argv.slice(2);
if (!baseUrl || !output || !['chrome','firefox'].includes(family)
    || !['snakes','sky-force'].includes(game) || !['on','off'].includes(counting)
    || !/^\d+$/.test(duration) || Number(duration) < 10)
  throw Error('Usage: node counting-comparison.ts URL NEW_OUTPUT chrome|firefox snakes|sky-force on|off [SECONDS]');
fs.mkdirSync(output);
const report: any = {started:new Date().toISOString(), family, game, counting,
  durationSeconds:Number(duration), sound:'muted', cpu:os.cpus()[0]?.model,
  menuKeyHoldMs:600,
  requiresSceneReview:true, loadBefore:os.loadavg(), samples:[], errors:[]};
const save = () => fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve,ms));
const browser = await puppeteer.launch({
  browser:family === 'firefox' ? 'firefox' : 'chrome',
  executablePath:family === 'firefox' ? '/usr/lib/firefox/firefox' : '/usr/bin/chromium',
  headless:true, acceptInsecureCerts:true,
  ...(family === 'firefox'
    ? {extraPrefsFirefox:{'webgl.force-enabled':true,'gfx.webrender.all':true}}
    : {args:['--no-sandbox','--disable-dev-shm-usage','--use-gl=angle','--use-angle=vulkan',
      '--enable-features=Vulkan','--enable-gpu','--ignore-gpu-blocklist']}),
});
try {
  report.browser = await browser.version();
  if (family === 'chrome') report.gpu = (await (await browser.target().createCDPSession()).send('SystemInfo.getInfo')).gpu;
  const page = await browser.newPage();
  await page.setViewport({width:900,height:800});
  await page.evaluateOnNewDocument(() => {
    const g=window as any;
    g.ekaFrameProbe={first:null,frames:[],last:0};
    const observe=() => {
      const m=g.Module, probe=g.ekaFrameProbe;
      if(m?.calledRun && m._eka2l1_presentations) {
        const count=m._eka2l1_presentations();
        if(count!==probe.last) {
          const sample={ms:performance.now(),count};
          if(!probe.first) probe.first={...sample,monitor:JSON.parse(m.ccall('eka2l1_monitor_report','string',[],[]))};
          if(probe.frames.length<32768)probe.frames.push(sample);
          probe.last=count;
        }
      }
      requestAnimationFrame(observe);
    };
    requestAnimationFrame(observe);
  });
  page.on('console',m => fs.appendFileSync(path.join(output,'browser.log'),m.text()+'\n'));
  page.on('pageerror',e => report.errors.push(String(e)));
  page.on('requestfailed',r => {
    if (!['net::ERR_ABORTED','NS_BINDING_ABORTED'].includes(r.failure()?.errorText ?? ''))
      report.errors.push(r.url()+': '+r.failure()?.errorText);
  });
  const landing = new URL(baseUrl);
  landing.searchParams.set('game','custom');
  landing.searchParams.set('counting',counting === 'on' ? 'off' : 'on');
  await page.goto(landing.href,{waitUntil:'domcontentloaded'});
  await page.waitForSelector('#instruction-counting');
  assert.equal(await page.$eval('#instruction-counting',e => (e as HTMLSelectElement).value),counting === 'on' ? 'off' : 'on');
  await page.select('#game-select',game);
  await page.select('#instruction-counting',counting);
  await page.setViewport({width:390,height:844});
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth),false);
  await page.screenshot({path:path.join(output,'mobile-picker.png')});
  await page.setViewport({width:900,height:800});
  await Promise.all([page.waitForNavigation({waitUntil:'domcontentloaded'}),page.click('#btn-play')]);
  assert.equal(new URL(page.url()).searchParams.get('counting'),counting);
  await page.waitForFunction(() => (window as any)._gameRunning,{timeout:180000});
  report.url = page.url();
  report.launch = await page.evaluate(() => {
    const g = window as any;
    const gl = document.createElement('canvas').getContext('webgl2');
    const debug = gl?.getExtension('WEBGL_debug_renderer_info');
    const renderer = debug ? gl!.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl?.getParameter(gl.RENDERER);
    gl?.getExtension('WEBGL_lose_context')?.loseContext();
    return {policy:g.ekaCompilerPolicy,assets:g.ekaAssetUrls,secure:isSecureContext,
      isolated:crossOriginIsolated,selected:(document.querySelector('#instruction-counting') as HTMLSelectElement).value,
      watchdog:g.Module._eka2l1_watchdog_report(),worker:!!g.ekaWatchdog,
      renderer,status:document.querySelector('#status')?.textContent};
  });
  assert.equal(report.launch.selected,counting);
  assert.equal(report.launch.watchdog,counting === 'off' ? 1 : 0);
  assert.equal(report.launch.worker,counting === 'off');
  assert.ok(report.launch.secure && report.launch.isolated && report.launch.policy.applied);
  assert.ok(report.launch.status.includes('Instruction counting: '+(counting === 'on' ? 'On' : 'Off')));
  const waitGuest = (us: number) => page.waitForFunction(t => (window as any).Module._eka2l1_guest_time_us() >= t,{timeout:180000},us);
  // Loading can pause guest execution for compilation. Absolute deadlines can
  // then send several key pairs before the game consumes any of them. Start
  // after a presentation and space presses from the current guest clock.
  await page.waitForFunction(() => (window as any).Module._eka2l1_presentations()>0,{timeout:180000});
  for (let i=0;i<(game === 'snakes' ? 10 : 7);++i) {
    const now=await page.evaluate(() => (window as any).Module._eka2l1_guest_time_us());
    await waitGuest(now+(game === 'snakes' ? 2 : 4)*1000000);
    await page.focus('#canvas');
    await page.keyboard.press('Enter',{delay:report.menuKeyHoldMs});
    await page.screenshot({path:path.join(output,`menu-${i}.png`)});
  }
  const afterMenus=await page.evaluate(() => (window as any).Module._eka2l1_guest_time_us());
  await waitGuest(afterMenus+8000000);
  await page.screenshot({path:path.join(output,'gameplay-start.png')});
  const inputBefore = await page.evaluate(() => (window as any).Module._eka2l1_input_consumed());
  await page.focus('#canvas');
  await page.keyboard.press('ArrowLeft',{delay:100});
  await page.keyboard.press('ArrowRight',{delay:100});
  await sleep(2000);
  assert.ok(await page.evaluate(() => (window as any).Module._eka2l1_input_consumed()) >= inputBefore+4);
  const sample = async () => {
    const before = performance.now();
    const state = await page.evaluate(() => {
      const g = window as any;
      const monitor = JSON.parse(g.Module.ccall('eka2l1_monitor_report','string',[],[]));
      return {guestUs:g.Module._eka2l1_guest_time_us(),frames:g.Module._eka2l1_presentations(),
        instructions:monitor.instructions,compiledFunctions:monitor.compiled_functions,
        compilation:monitor,browserMs:performance.now(),
        inputs:g.Module._eka2l1_input_consumed(),watchdog:g.Module._eka2l1_watchdog_report()};
    });
    const after = performance.now();
    return {hostMs:(before+after)/2,queryMs:after-before,...state};
  };
  report.samples.push(await sample());
  console.log('MEASURING '+JSON.stringify({family,game,counting,duration:report.durationSeconds,renderer:report.launch.renderer}));
  save();
  const begin = performance.now();
  while (performance.now()-begin < report.durationSeconds*1000) {
    await sleep(500);
    report.samples.push(await sample());
  }
  const first = report.samples[0], last = report.samples.at(-1);
  const hostSeconds = (last.hostMs-first.hostMs)/1000;
  report.measurement = {hostSeconds,guestSeconds:(last.guestUs-first.guestUs)/1e6,
    guestSpeed:(last.guestUs-first.guestUs)/1e6/hostSeconds,
    presentations:last.frames-first.frames,presentationsPerSecond:(last.frames-first.frames)/hostSeconds,
    instructions:last.instructions-first.instructions,
    installedCompiledFunctions:last.compiledFunctions-first.compiledFunctions,
    maxQueryMs:Math.max(...report.samples.map((s:any) => s.queryMs))};
  report.frameProbe=await page.evaluate(() => (window as any).ekaFrameProbe);
  const frameTimes=report.frameProbe.frames.filter((f:any) => f.ms>=first.browserMs && f.ms<=last.browserMs);
  report.measurement.maxObservedFrameGapMs=frameTimes.length>1
    ? Math.max(...frameTimes.slice(1).map((f:any,i:number) => f.ms-frameTimes[i].ms)) : null;
  report.measurement.compilationUs=Object.fromEntries(
    ['aot_translation_us','aot_emission_us','aot_installation_us'].map(k => [k,last.compilation[k]-first.compilation[k]]));
  report.loadAfter = os.loadavg();
  assert.ok(report.measurement.presentations > 0);
  if (counting === 'on') assert.ok(report.measurement.instructions > 0);
  else assert.ok(report.samples.every((s:any) => s.instructions === 0));
  assert.ok(report.samples.every((s:any) => s.watchdog === report.launch.watchdog));
  // Screenshots are outside the timing window; Firefox capture can be slow.
  await page.screenshot({path:path.join(output,'gameplay-end.png')});
  assert.deepEqual(report.errors,[]);
  report.passed = true;
  console.log('RESULT '+JSON.stringify(report.measurement));
} catch (error) {
  report.errors.push(String(error)); throw error;
} finally {
  save(); await browser.close();
}
