import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import puppeteer from 'puppeteer';
import {PNG} from 'pngjs';
import {startServer, buildDir} from './server.ts';

const [assetArg, outputArg, frameArg = '1000', inputArg = '../benchmark/snakes.input', startArg = '21000000'] = process.argv.slice(2);
if (!assetArg || !outputArg) throw new Error('Usage: node benchmark.ts ASSETS NEW_OUTPUT [FRAMES] [INPUT] [START_US]');
const sharedAudio = process.env.EKA2L1_SHARED_AUDIO === "1";
const glDiagnostics = process.env.EKA2L1_GL_DIAGNOSTICS === "1";
const aotDiagnostics = process.env.EKA2L1_AOT_DIAGNOSTICS === "1";
const tlbHash = process.env.EKA2L1_TLB_HASH === undefined ? -1 : Number(process.env.EKA2L1_TLB_HASH);
if (![-1,0,1].includes(tlbHash)) throw new Error('Invalid TLB index policy');
const codeWriteProtect = process.env.EKA2L1_CODE_WRITE_PROTECT === undefined ? -1 : Number(process.env.EKA2L1_CODE_WRITE_PROTECT);
if (![-1,0,1].includes(codeWriteProtect)) throw new Error('Invalid code write protection policy');
const codeLookup = process.env.EKA2L1_CODE_LOOKUP === undefined ? -1 : Number(process.env.EKA2L1_CODE_LOOKUP);
if (![-1,0,1].includes(codeLookup)) throw new Error('Invalid code lookup policy');
const codeCompare = process.env.EKA2L1_CODE_COMPARE === undefined ? -1 : Number(process.env.EKA2L1_CODE_COMPARE);
if (![-1,0,1,2,3,4].includes(codeCompare)) throw new Error('Invalid exact comparison policy');
const exitCensus = process.env.EKA2L1_EXIT_CENSUS === '1';
const leafFeatures=Number(process.env.EKA2L1_LEAF_FEATURES || '0');
if(!Number.isInteger(leafFeatures) || leafFeatures<0 || leafFeatures>31)throw Error('Invalid leaf feature mask');
const predicatedLeaves = Number(process.env.EKA2L1_PREDICATED_LEAVES || '0');
if(![0,1].includes(predicatedLeaves))throw Error('Invalid leaf predication setting');
const limitsText = process.env.EKA2L1_EXECUTION_LIMITS || '512,16,8,512';
const executionLimits = limitsText.split(',').map(Number);
if (!/^\d+,\d+,\d+,\d+$/.test(limitsText) || executionLimits.length!==4 || executionLimits.some(n=>!Number.isSafeInteger(n))
    || executionLimits[0]<128 || executionLimits[0]>2048 || executionLimits[0]%4 || executionLimits[1]<1 || executionLimits[1]>64
    || executionLimits[2]<0 || executionLimits[2]>16 || executionLimits[3]<0 || executionLimits[3]>4096) throw new Error('Invalid execution limits');
const irMode = process.env.EKA2L1_AOT_IR_MODE === undefined ? -1 : Number(process.env.EKA2L1_AOT_IR_MODE);
if (![-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16].includes(irMode)) throw new Error('IR mode must be -1 (configured), 0 (disabled), 1 (inline) or 2 (outlined) or 3 (exit recipes) or 4 (invariant reads without IR) or 5 (invariant reads and writes without IR) or 6 (read proofs and budget chunks) or 7 (write proofs and budget chunks) or 8 (deferred chunk counts) or 9 (IR with invariant read proofs) or 10 (IR flags with read proofs) or 11 (IR through inline leaves) or 12 (IR with read/write proofs) or 13 (conditional integer values) or 14 (longer bounded IR segments) or 15 (single-use pure stack values) or 16 (IR with budget chunks in gaps)');
const eagerRegions = process.env.EKA2L1_AOT_EAGER_REGIONS === undefined ? -1 : Number(process.env.EKA2L1_AOT_EAGER_REGIONS);
if (![-1,0,1].includes(eagerRegions)) throw new Error('Eager regions must be 0 (off) or 1 (on)');
const verifyAot = Number(process.env.EKA2L1_AOT_VERIFY || "0");
if (!Number.isSafeInteger(verifyAot) || verifyAot < 0 || verifyAot > 2147483647)
  throw new Error('EKA2L1_AOT_VERIFY must be a nonnegative integer stride');
const aot = Number(process.env.EKA2L1_BENCHMARK_AOT || "0");
if (![0,1,2,3,4,5].includes(aot)) throw new Error("AOT mode must be 0, 1, 2, 3, 4 or 5");
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
const hash = (data: Uint8Array) => crypto.createHash('sha256').update(data).digest('hex');
for (const [name, digest] of Object.entries(expected))
  if (hash(fs.readFileSync(path.join(assets, name))) !== digest) throw new Error(`Bad asset: ${name}`);
const wasmHash = hash(fs.readFileSync(path.join(buildDir, 'eka2l1.wasm')));
const inputHash = hash(fs.readFileSync(input));
const gitHead = execFileSync('git', ['rev-parse', 'HEAD'], {encoding: 'utf8'}).trim();
const dirtyWorktree = !!execFileSync('git', ['status', '--porcelain'], {encoding: 'utf8'}).trim();
fs.mkdirSync(output); // Refuse to mix captures from different runs.
const files: Record<string,string> = {'/preload/input': input};
for (const name of Object.keys(expected)) files[`/preload/${name}`] = path.join(assets, name);
const {server, port} = await startServer(0, files);
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
  const glDiagnosticsSupported = await page.evaluate(() => typeof (window as any).Module._eka2l1_graphics_diagnostics_configure === 'function');
  await page.evaluate(async ({tlbHash, codeCompare, codeLookup, codeWriteProtect, eagerRegions, irMode, exitCensus, predicatedLeaves, leafFeatures, executionLimits, count, startUs, aot, verifyAot, aotDiagnostics, glDiagnostics, sharedAudio}) => {
    const g = window as any;
    const call = (name: string, types: string[], args: unknown[]) => {
      const code = g.Module.ccall(name, 'number', types, args);
      if (code !== 0) throw new Error(`${name} returned ${code}`);
    };
    call('eka2l1_benchmark_configure', ['number', 'number', 'number'], [count, startUs, 1]);
    call('eka2l1_aot_configure', ['number', 'number', 'number'], [aot, verifyAot, aotDiagnostics ? 1 : 0]);
    if (eagerRegions !== -1) {
      if (typeof g.Module._eka2l1_eager_regions_configure !== 'function') throw new Error('Build lacks eager region selection');
      call('eka2l1_eager_regions_configure', ['number'], [eagerRegions]);
    }
    if (tlbHash !== -1) {
      if (typeof g.Module._eka2l1_tlb_hash_configure !== 'function') throw new Error('Build lacks TLB index selection');
      call('eka2l1_tlb_hash_configure', ['number'], [tlbHash]);
    }
    if (codeWriteProtect !== -1) {
      if (typeof g.Module._eka2l1_code_write_protect_configure !== 'function') throw new Error('Build lacks code write protection selection');
      call('eka2l1_code_write_protect_configure', ['number'], [codeWriteProtect]);
    }
    if (codeLookup !== -1) {
      if (typeof g.Module._eka2l1_code_lookup_configure !== 'function') throw new Error('Build lacks code lookup selection');
      call('eka2l1_code_lookup_configure', ['number'], [codeLookup]);
    }
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
    if(typeof g.Module._eka2l1_leaf_features_configure==='function') {
      call('eka2l1_leaf_features_configure',['number'],[leafFeatures]);
      if(g.Module.ccall('eka2l1_leaf_features_report','number',[],[])!==leafFeatures)throw Error('Leaf features were not applied');
    } else if(leafFeatures)throw Error('Leaf features API unavailable');
    if (typeof g.Module._eka2l1_execution_limits_configure === 'function') {
      call('eka2l1_execution_limits_configure', ['number','number','number','number'], executionLimits);
      const observed=g.Module.ccall('eka2l1_execution_limits_report','string',[],[]);
      if(observed!==executionLimits.join(','))throw new Error('Execution limits were not applied');
    } else if(executionLimits.join(',')!=='512,16,8,512')throw new Error('Execution limits API unavailable');
    if (typeof g.Module._eka2l1_graphics_diagnostics_configure === 'function')
      call('eka2l1_graphics_diagnostics_configure', ['number'], [glDiagnostics ? 1 : 0]);
    else if (glDiagnostics) throw new Error('Build does not support graphics diagnostic configuration');
    if (sharedAudio) call('eka2l1_audio_configure', [], []);
    call('eka2l1_init', ['string'], ['/data']);
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
    call('eka2l1_run', ['string'], ['Snakes']);
  }, {tlbHash, codeCompare, codeLookup, codeWriteProtect, eagerRegions, irMode, exitCensus, predicatedLeaves, leafFeatures, executionLimits, count: frames, startUs, aot, verifyAot, aotDiagnostics, glDiagnostics, sharedAudio});
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
  fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({frames, start_us: startUs, unique: true, wall_seconds: (performance.now()-start)/1000,
    assets: expected, input_sha256: inputHash, wasm_sha256: wasmHash, gl_diagnostics: glDiagnostics || !glDiagnosticsSupported, gl_diagnostics_configurable: glDiagnosticsSupported,
    shared_audio: sharedAudio, aot, aot_diagnostics: aotDiagnostics, ir_mode: irMode, execution_limits:executionLimits, predicated_leaves:predicatedLeaves, leaf_features:leafFeatures, exit_census:exitCensus, tlb_hash: tlbHash, code_compare: codeCompare, code_lookup: codeLookup, code_write_protect: codeWriteProtect, eager_regions: eagerRegions, verify_aot: verifyAot, git_head: gitHead, dirty_worktree: dirtyWorktree}, null, 2));
  console.log('PASS: captured benchmark');
} finally {
  await browser?.close();
  server.close();
  log.end();
}
