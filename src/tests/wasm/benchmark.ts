import {configureWatchdog, watchdogInterval} from './watchdog.ts';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import puppeteer from 'puppeteer';
import {PNG} from 'pngjs';
import {startServer, buildDir, compilerDefaults, rejectRetiredCompilerOptions} from './server.ts';

const [assetArg, outputArg, frameArg = '1000', inputArg = '../benchmark/watchdog-snakes-countfree.input', startArg = '42000000'] = process.argv.slice(2);
if (!assetArg || !outputArg) throw new Error('Usage: node benchmark.ts ASSETS NEW_OUTPUT [FRAMES] [INPUT] [START_US]');
const sparseRom = Number(process.env.EKA2L1_SPARSE_ROM_LOOKUP ?? compilerDefaults.sparseRom);
if (![0,1].includes(sparseRom)) throw Error('sparseRom must be 0 or 1');
const compiledSvc = Number(process.env.EKA2L1_COMPILED_SVC ?? compilerDefaults.compiledSvc);
if (![0,1].includes(compiledSvc)) throw Error('Compiled syscall policy must be 0 or 1');
const thumbMemory = Number(process.env.EKA2L1_THUMB_MEMORY ?? compilerDefaults.thumbMemory);
if (![-1,0,1].includes(thumbMemory)) throw Error('Invalid Thumb memory policy');
rejectRetiredCompilerOptions();
const armExclusive = process.env.EKA2L1_ARM_EXCLUSIVE === undefined ? -1 : Number(process.env.EKA2L1_ARM_EXCLUSIVE);
if (![-1,0,1].includes(armExclusive)) throw Error('ARM exclusive must be 0 or 1');
const appUid = process.env.EKA2L1_APP_UID || '0x2000730f';
if (!/^0x[0-9a-fA-F]{1,8}$/.test(appUid) || Number(appUid) === 0) throw Error('Invalid application UID');
const sharedAudio = process.env.EKA2L1_SHARED_AUDIO === "1";
const glDiagnostics = process.env.EKA2L1_GL_DIAGNOSTICS === "1";
const aotDiagnostics = process.env.EKA2L1_AOT_DIAGNOSTICS === "1";
if (watchdogInterval() && (Number(process.env.EKA2L1_AOT_VERIFY || '0') > 0
    || aotDiagnostics || process.env.EKA2L1_EXIT_CENSUS === '1'))
  throw Error('Watchdog requires verification and instruction diagnostics off');
const codeCompare = process.env.EKA2L1_CODE_COMPARE === undefined ? -1 : Number(process.env.EKA2L1_CODE_COMPARE);
if (![-1,0,2].includes(codeCompare)) throw new Error('Invalid exact comparison policy');
const exitCensus = process.env.EKA2L1_EXIT_CENSUS === '1';
const unsafeText=process.env.EKA2L1_UNSAFE_CODE ?? '3';
if(!/^[03]$/.test(unsafeText))throw Error('Invalid unsafe code mode');
if (process.env.EKA2L1_DIRECT_POLICY !== undefined || process.env.EKA2L1_MEMORY_ACTIVATE_US !== undefined)
  throw Error('Direct policy and delayed activation were removed; select EKA2L1_MEMORY_IMPL=0 (TLB) or 2 (direct)');
const unsafeCode=Number(unsafeText);
if(![0,3].includes(unsafeCode))throw Error('Invalid unsafe code mode');
const leafFeatures=Number(process.env.EKA2L1_LEAF_FEATURES ?? compilerDefaults.leafFeatures);
if(![0,32,64,96,128,160,192,224].includes(leafFeatures))throw Error('Invalid leaf feature mask');
const predicatedLeaves = Number(process.env.EKA2L1_PREDICATED_LEAVES ?? compilerDefaults.predicatedLeaves);
if(![0,1].includes(predicatedLeaves))throw Error('Invalid leaf predication setting');
const executionLimits = compilerDefaults.executionLimits.split(',').map(Number);
const irMode = Number(process.env.EKA2L1_AOT_IR_MODE ?? compilerDefaults.irMode);
if (![-1,0,4,5,6,7].includes(irMode)) throw Error('Invalid compiler policy');
const hotpathPolicy = Number(process.env.EKA2L1_HOTPATH ?? compilerDefaults.hotpath);
if (![-1,0,2].includes(hotpathPolicy)) throw Error('Hotpath policy must be 0 or 2');
const verifyAot = Number(process.env.EKA2L1_AOT_VERIFY || "0");
if (!Number.isSafeInteger(verifyAot) || verifyAot < 0 || verifyAot > 2147483647)
  throw new Error('EKA2L1_AOT_VERIFY must be a nonnegative integer stride');
const aot = Number(process.env.EKA2L1_BENCHMARK_AOT || "0");
if (![0,5].includes(aot)) throw new Error("AOT mode must be 0 (interpreter) or 5 (compiler)");
// Direct memory is the compiled-play default; interpreter/verifier runs need TLB.
const memoryText = process.env.EKA2L1_MEMORY_IMPL ?? (aot === 5 && !verifyAot && unsafeCode === 3 ? '2' : '0');
if (!/^[02]$/.test(memoryText)) throw Error('Memory implementation must be 0 (TLB) or 2 (direct)');
const memoryImpl = Number(memoryText);
const assets = path.resolve(assetArg), output = path.resolve(outputArg), frames = Number(frameArg);
if (!Number.isInteger(frames) || frames < 1 || frames > 100000) throw new Error('Invalid frame count');
const input = path.resolve(inputArg);
const startUs = Number(startArg);
if (!Number.isInteger(startUs) || startUs < 0 || startUs > 120000000) throw new Error('Invalid start time');
const expected: Record<string,string> = {
  'SYM.ROM': '89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490',
  'SYM.RPKG': '58964f3d08a542f01118a7dfb78a34d2e029962b8edb9988381a37994c1c1531',
  'Snakes.sis': '14d9a40768ae2231ad1905e96bcedefe7ce97b7fc4e610bb924ff8037a6cf82a',
};
if (process.env.EKA2L1_ASSET_MANIFEST) {
  const manifest = JSON.parse(fs.readFileSync(process.env.EKA2L1_ASSET_MANIFEST, 'utf8'));
  for (const name of Object.keys(expected)) {
    const digest = manifest.assets?.[name]?.sha256;
    if (typeof digest !== 'string' || !/^[0-9a-f]{64}$/.test(digest)) throw new Error(`Invalid asset digest: ${name}`);
    expected[name] = digest;
  }
}
const hash = (data: Uint8Array) => crypto.createHash('sha256').update(data).digest('hex');
for (const [name, digest] of Object.entries(expected))
  if (hash(fs.readFileSync(path.join(assets, name))) !== digest) throw new Error(`Bad asset: ${name}`);
const wasmHash = hash(fs.readFileSync(path.join(buildDir, 'eka2l1.wasm')));
const loaderHash = hash(fs.readFileSync(path.join(buildDir, 'eka2l1.js')));
const inputHash = hash(fs.readFileSync(input));
const startFrame = process.env.EKA2L1_BENCHMARK_START_FRAME
  ? PNG.sync.read(fs.readFileSync(process.env.EKA2L1_BENCHMARK_START_FRAME)) : null;
const gitHead = execFileSync('git', ['rev-parse', 'HEAD'], {encoding: 'utf8'}).trim();
const dirtyWorktree = !!execFileSync('git', ['status', '--porcelain'], {encoding: 'utf8'}).trim();
fs.mkdirSync(output); // Refuse to mix captures from different runs.
const files: Record<string,string> = {'/preload/input': input};
for (const name of Object.keys(expected)) files[`/preload/${name}`] = path.join(assets, name);
const {server, port} = await startServer(0, files, undefined, {compilerPolicy: {}});
const log = fs.createWriteStream(path.join(output, 'browser.log'));
let browser;
try {
  browser = await puppeteer.launch({
    executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium',
    headless: true,
    protocolTimeout: 1800000,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=swiftshader',
           '--enable-unsafe-swiftshader', '--disable-background-timer-throttling'],
  });
  const page = await browser.newPage();
  const failures: string[] = [];
  page.on('console', msg => {log.write(`${msg.type()}: ${msg.text()}\n`); if (msg.text().includes('ABORT:')) failures.push(msg.text());});
  page.on('pageerror', error => failures.push(String(error)));
  page.on('requestfailed', request => failures.push(`${request.url()}: ${request.failure()?.errorText}`));
  page.on('response', response => {if (response.status() >= 400) failures.push(`HTTP ${response.status()} ${response.url()}`);});
  await page.goto(`http://127.0.0.1:${port}/`, {waitUntil: 'domcontentloaded'});
  await page.waitForFunction(() => (window as any).Module?.calledRun, {timeout: 120000});
  await page.evaluate(configureWatchdog, watchdogInterval());
  if (startFrame) await page.evaluate((pixels) => {
    const g = globalThis as any;
    g.FS.writeFile('/benchmark-start.rgba', new Uint8Array(pixels));
    if (g.Module.ccall('eka2l1_benchmark_start_frame', 'number', ['string'], ['/benchmark-start.rgba']) !== 0)
      throw Error('Start-frame configuration failed');
  }, Array.from(startFrame.data));
  const glDiagnosticsSupported = await page.evaluate(() => typeof (window as any).Module._eka2l1_graphics_diagnostics_configure === 'function');
  await page.evaluate(async ({ sparseRom, compiledSvc, hotpathPolicy, armExclusive, thumbMemory, appUid, codeCompare, irMode, exitCensus, predicatedLeaves, leafFeatures, unsafeCode, memoryImpl, executionLimits, count, startUs, aot, verifyAot, aotDiagnostics, glDiagnostics, sharedAudio}) => {
    const g = window as any;
    const call = (name: string, types: string[], args: unknown[]) => {
      const code = g.Module.ccall(name, 'number', types, args);
      if (code !== 0) throw new Error(`${name} returned ${code}`);
    };
    call('eka2l1_benchmark_configure', ['number', 'number', 'number'], [count, startUs, 1]);
    call('eka2l1_aot_configure', ['number', 'number', 'number'], [aot, verifyAot, aotDiagnostics ? 1 : 0]);
    if (hotpathPolicy !== -1) {
      call('eka2l1_hotpath_configure', ['number'], [hotpathPolicy]);
      if (g.Module._eka2l1_hotpath_configure(-1) !== -1 || g.Module._eka2l1_hotpath_configure(8) !== -1)
        throw Error('Invalid hotpath policy accepted');
    }
    g.hotpathActual = typeof g.Module._eka2l1_hotpath_report === 'function' ? g.Module._eka2l1_hotpath_report() : null;
    if (hotpathPolicy !== -1 && g.hotpathActual !== hotpathPolicy) throw Error('Hotpath readback mismatch');

    if (codeCompare !== -1) {
      if (typeof g.Module._eka2l1_code_compare_configure !== 'function') throw new Error('Build lacks exact comparison selection');
      call('eka2l1_code_compare_configure', ['number'], [codeCompare]);
    }
    if (irMode !== -1) {
      if (typeof g.Module._eka2l1_ir_configure !== 'function') throw new Error('Build lacks IR mode selection');
      call('eka2l1_ir_configure', ['number'], [irMode]);
    }
    if(exitCensus) {
      call('eka2l1_exit_census_configure',['number'],[1]);
      if(g.Module.ccall('eka2l1_exit_census_report','number',[],[])!==1)throw Error('Exit census was not applied');
    }
    if(typeof g.Module._eka2l1_leaf_predication_configure==='function') {
      call('eka2l1_leaf_predication_configure',['number'],[predicatedLeaves]);
      if(g.Module.ccall('eka2l1_leaf_predication_report','number',[],[])!==predicatedLeaves)throw Error('Leaf predication was not applied');
    } else if(predicatedLeaves)throw Error('Leaf predication API unavailable');
    if(typeof g.Module._eka2l1_unsafe_code_configure==='function') {
      g.unsafeCodeInitial=g.Module.ccall('eka2l1_unsafe_code_report','number',[],[]);
      call('eka2l1_unsafe_code_configure',['number'],[unsafeCode]);
      const actual=g.Module.ccall('eka2l1_unsafe_code_report','number',[],[]);
      if(actual!==unsafeCode)throw Error('Unsafe code mode was not applied');
      g.unsafeCodeActual=actual;
    } else if(unsafeCode)throw Error('Unsafe code API unavailable');
    else g.unsafeCodeActual=0;
    if(typeof g.Module._eka2l1_leaf_features_configure==='function') {
      call('eka2l1_leaf_features_configure',['number'],[leafFeatures]);
      if(g.Module.ccall('eka2l1_leaf_features_report','number',[],[])!==leafFeatures)throw Error('Leaf features were not applied');
    } else if(leafFeatures)throw Error('Leaf features API unavailable');
    if (typeof g.Module._eka2l1_execution_limits_report !== 'function'
        || g.Module.ccall('eka2l1_execution_limits_report','string',[],[]) !== executionLimits.join(','))
      throw Error('Fixed execution limits readback mismatch');
    if (typeof g.Module._eka2l1_graphics_diagnostics_configure === 'function')
      call('eka2l1_graphics_diagnostics_configure', ['number'], [glDiagnostics ? 1 : 0]);
    else if (glDiagnostics) throw new Error('Build does not support graphics diagnostic configuration');
    if (sharedAudio) call('eka2l1_audio_configure', [], []);
    if (thumbMemory !== -1) {
      call('eka2l1_thumb_memory_configure', ['number'], [thumbMemory]);
      if (g.Module.ccall('eka2l1_thumb_memory_report', 'number', [], []) !== thumbMemory)
        throw Error('Thumb memory policy readback mismatch');
    }
    call('eka2l1_sparse_rom_lookup_configure', ['number'], [sparseRom]);
    g.sparseRomActual = g.Module._eka2l1_sparse_rom_lookup_report();
    if (g.sparseRomActual !== sparseRom) throw Error('sparseRom readback mismatch');
    call('eka2l1_compiled_svc_configure', ['number'], [compiledSvc]);
    g.compiledSvcActual = g.Module._eka2l1_compiled_svc_report();
    if (g.compiledSvcActual !== compiledSvc) throw Error('Compiled syscall readback mismatch');
    if (armExclusive !== -1) call('eka2l1_arm_exclusive_configure', ['number'], [armExclusive]);
    g.armExclusiveActual = typeof g.Module._eka2l1_arm_exclusive_report === 'function' ? g.Module._eka2l1_arm_exclusive_report() : null;
    if (armExclusive !== -1 && g.armExclusiveActual !== armExclusive) throw Error('ARM exclusive readback mismatch');
    if (g.Module._eka2l1_arm_memory_configure || g.Module._eka2l1_arm_memory_report)
      throw Error('Retired ARM memory configuration API is still exported');
    if(typeof g.Module._eka2l1_memory_impl_configure==='function') {
      call('eka2l1_memory_impl_configure',['number'],[memoryImpl]);
      for(const removed of [-1,1,3,4]) {
        if(g.Module._eka2l1_memory_impl_configure(removed)!==-1)throw Error('Removed memory implementation was accepted');
        if(g.Module._eka2l1_memory_impl_report()!==memoryImpl)throw Error('Rejected memory configuration changed the active mode');
      }
      if(g.Module._eka2l1_direct_memory_configure || g.Module._eka2l1_memory_impl_activation)
        throw Error('Removed direct policy or activation API is still exported');
    } else if(memoryImpl)throw Error('Memory implementation API unavailable');
    call('eka2l1_init', ['string'], ['/data']);
    if(typeof g.Module._eka2l1_memory_impl_configure==='function' && g.Module._eka2l1_memory_impl_configure(memoryImpl)!==-1)
      throw Error('Memory implementation was not frozen');
    if (hotpathPolicy !== -1 && (g.Module._eka2l1_hotpath_configure(hotpathPolicy ^ 2) !== -1
        || g.Module._eka2l1_hotpath_report() !== hotpathPolicy)) throw Error('Hotpath policy changed after initialization');
    if (thumbMemory !== -1 && g.Module._eka2l1_thumb_memory_configure(1 - thumbMemory) !== -1)
      throw Error('Thumb memory policy changed after initialization');
    if (g.Module._eka2l1_sparse_rom_lookup_configure(1-sparseRom) !== -1) throw Error('sparseRom changed after initialization');
    if (g.Module._eka2l1_compiled_svc_configure(1-compiledSvc) !== -1) throw Error('Compiled syscall policy changed after initialization');
    if (armExclusive !== -1 && g.Module._eka2l1_arm_exclusive_configure(1-armExclusive) !== -1) throw Error('ARM exclusive changed after initialization');
    if(typeof g.Module._eka2l1_unsafe_code_report==='function') {
      if(g.Module._eka2l1_unsafe_code_report()!==unsafeCode || g.Module._eka2l1_unsafe_code_configure(unsafeCode===3?0:3)!==-1)
        throw Error('Unsafe mode changed or remained configurable after CPU initialization');
    }
    for (const name of ['SYM.ROM', 'SYM.RPKG', 'Snakes.sis', 'input']) {
      const response = await fetch(`/preload/${name}`);
      if (!response.ok) throw new Error(`Asset HTTP ${response.status()}`);
      g.FS.writeFile(name === 'input' ? '/benchmark.input' : `/tmp/${name}`, new Uint8Array(await response.arrayBuffer()));
    }
    call('eka2l1_install_device', ['string', 'string'], ['/tmp/SYM.ROM', '/tmp/SYM.RPKG']);
    call('eka2l1_install_sis', ['string'], ['/tmp/Snakes.sis']);
    // Match the live launcher's storage lifetime after synchronous installation.
    for (const name of ['SYM.ROM', 'SYM.RPKG', 'Snakes.sis']) g.FS.unlink(`/tmp/${name}`);
    g.FS.mkdir('/frames');
    g.Module.ccall('eka2l1_start_frame_dump', null, ['string', 'number'], ['/frames', count]);
    if (g.Module._eka2l1_prepare_graphics) {
      call('eka2l1_prepare_graphics', [], []);
      const deadline = performance.now() + 30000;
      while (g.Module._eka2l1_graphics_ready() === 0) {
        if (performance.now() > deadline) throw new Error('Graphics initialization timeout');
        await new Promise(resolve => setTimeout(resolve, 10));
      }
    }
    // N80 also registers a different ROM-bundled game with the caption Snakes.
    call('eka2l1_run', ['string'], [appUid]);
  }, { sparseRom, compiledSvc, hotpathPolicy, armExclusive, thumbMemory, appUid, codeCompare, irMode, exitCensus, predicatedLeaves, leafFeatures, unsafeCode, memoryImpl, executionLimits, count: frames, startUs, aot, verifyAot, aotDiagnostics, glDiagnostics, sharedAudio});
  const start = performance.now();
  let lastCount = -1;
  let firstCanvas: Buffer | undefined;
  while (true) {
    if (failures.length) throw new Error(failures.join('\n'));
    const state = await page.evaluate(() => {
      const m = (window as any).Module;
      return {done: m._eka2l1_frame_dump_done(), captured: m._eka2l1_frame_dump_captured()};
    });
    if (state.captured !== lastCount) {
      console.log(`${state.captured}/${frames} frames, ${((performance.now()-start)/1000).toFixed(1)}s`);
      lastCount = state.captured;
    }
    if (!firstCanvas && state.captured > 0 && state.captured < frames) {
      firstCanvas = Buffer.from(await (await page.$('#canvas'))!.screenshot());
      fs.writeFileSync(path.join(output, 'visible-first.png'), firstCanvas);
    }
    if (state.done) break;
    if (performance.now() - start > 1800000) throw new Error('Benchmark timeout');
    // Observes progress only; no host callbacks deliver guest input or advance time.
    await new Promise(resolve => setTimeout(resolve, 500));
  }
  if (lastCount !== frames) throw new Error(`Expected ${frames} frames, got ${lastCount}`);
  const names = await page.evaluate(() => (window as any).FS.readdir('/frames').filter((name: string) => name !== '.' && name !== '..')) as string[];
  for (let i = 0; i < names.length; i += 25) {
    const data = await page.evaluate((batch) => batch.map(name => {
      const bytes = (window as any).FS.readFile(`/frames/${name}`);
      let text = '';
      for (let offset = 0; offset < bytes.length; offset += 8192)
        text += String.fromCharCode(...bytes.subarray(offset, offset+8192));
      return [name, btoa(text)];
    }), names.slice(i, i+25));
    for (const [name, bytes] of data) fs.writeFileSync(path.join(output, name), Buffer.from(bytes, 'base64'));
  }
  const audio = JSON.parse(execFileSync('python3', [fileURLToPath(new URL('../benchmark/validate_audio.py', import.meta.url)), output],
    {encoding: 'utf8', env: {...process.env, PYTHONDONTWRITEBYTECODE: '1'}}));
  fs.writeFileSync(path.join(output, 'audio.json'), JSON.stringify(audio, null, 2));
  if (startFrame && !PNG.sync.read(fs.readFileSync(path.join(output, 'frame-0000.png'))).data.equals(startFrame.data))
    throw Error('First captured frame does not match the requested start frame');
  await page.screenshot({path: path.join(output, 'browser.png')});
  const canvasBytes = Buffer.from(await (await page.$('#canvas'))!.screenshot());
  fs.writeFileSync(path.join(output, 'visible-last.png'), canvasBytes);
  const canvasPixels = PNG.sync.read(canvasBytes).data;
  const visibleColors = new Set<number>();
  for (let i = 0; i < canvasPixels.length; i += 4)
    visibleColors.add((canvasPixels[i] << 16) | (canvasPixels[i+1] << 8) | canvasPixels[i+2]);
  if (visibleColors.size < 32) throw new Error('Visible canvas is blank or trivial');
  if (firstCanvas && PNG.sync.read(firstCanvas).data.equals(canvasPixels)) throw new Error('Visible canvas did not change');

  await page.evaluate(() => (window as any).Module._eka2l1_shutdown());
  if (failures.length) throw new Error(failures.join('\n'));
  fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({start_frame_sha256: startFrame ? hash(startFrame.data) : null, watchdog_us: watchdogInterval(), watchdog_requests: await page.evaluate(() => (globalThis as any).ekaWatchdog ? Atomics.load((globalThis as any).ekaWatchdog.control, 1) : 0), sparse_rom_lookup: await page.evaluate(() => (globalThis as any).sparseRomActual), compiled_svc: await page.evaluate(() => (globalThis as any).compiledSvcActual), hotpath_policy: await page.evaluate(() => (globalThis as any).hotpathActual), arm_exclusive: await page.evaluate(() => (globalThis as any).armExclusiveActual), thumb_memory: thumbMemory, app_uid: appUid, frames, start_us: startUs, unique: true, wall_seconds: (performance.now()-start)/1000,
    assets: expected, input_sha256: inputHash, wasm_sha256: wasmHash, loader_sha256: loaderHash, gl_diagnostics: glDiagnostics || !glDiagnosticsSupported, gl_diagnostics_configurable: glDiagnosticsSupported,
    shared_audio: sharedAudio, aot, aot_diagnostics: aotDiagnostics, memory_impl:memoryImpl, memory_impl_stats:await page.evaluate(() => {const m=(globalThis as any).Module;return m._eka2l1_memory_impl_stats?JSON.parse(m.ccall('eka2l1_memory_impl_stats','string',[],[])):null;}), ir_mode: irMode, execution_limits:executionLimits, predicated_leaves:predicatedLeaves, leaf_features:leafFeatures, unsafe_code_initial:await page.evaluate(() => (globalThis as any).unsafeCodeInitial ?? null), unsafe_code:await page.evaluate(() => (globalThis as any).unsafeCodeActual), exit_census:exitCensus, code_compare: codeCompare, verify_aot: verifyAot, git_head: gitHead, dirty_worktree: dirtyWorktree}, null, 2));
  console.log('PASS: captured benchmark');
} finally {
  await browser?.close();
  server.close();
  log.end();
}
