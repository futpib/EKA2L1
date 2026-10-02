import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import puppeteer from 'puppeteer';
import {startServer, buildDir} from './server.ts';

const [assetArg, outputArg, modeArg = '0', samplingArg = '1', endArg = '25000000'] = process.argv.slice(2);
const frameArg = '100000', inputArg = process.env.EKA2L1_PROFILE_INPUT || fileURLToPath(new URL('../benchmark/snakes.input', import.meta.url)), startArg = process.env.EKA2L1_PROFILE_START_US || '21000000';
const captureMode = Number(modeArg), sampling = samplingArg === '1', endUs = Number(endArg);
const sampleInterval = Number(process.env.EKA2L1_PROFILE_INTERVAL_US || '1000');
if (!Number.isSafeInteger(sampleInterval) || sampleInterval < 100 || sampleInterval > 1000000)
  throw new Error('Profile sample interval must be between 100 and 1000000 microseconds');
if (![0,1,2].includes(captureMode) || !Number.isInteger(endUs) || endUs <= Number(startArg) || endUs > 1800000000) throw new Error('Invalid profile settings');
if (!assetArg || !outputArg) throw new Error('Usage: node profile.ts ASSETS NEW_OUTPUT [CAPTURE_MODE:0/1/2] [SAMPLING:0/1] [END_US]');
const guestProfile = Number(process.env.EKA2L1_GUEST_PROFILE || "0");
if (!Number.isSafeInteger(guestProfile) || guestProfile < 0 || guestProfile > 2147483647)
  throw new Error('EKA2L1_GUEST_PROFILE must be a nonnegative sample stride');
const exitCensus = process.env.EKA2L1_EXIT_CENSUS === "1";
if(exitCensus && !guestProfile) throw new Error("Exit census requires guest profiling");
const thumbMemory = process.env.EKA2L1_THUMB_MEMORY === undefined ? -1 : Number(process.env.EKA2L1_THUMB_MEMORY);
if (![-1,0,1].includes(thumbMemory)) throw Error('Invalid Thumb memory policy');
const armMemory = process.env.EKA2L1_ARM_MEMORY === undefined ? -1 : Number(process.env.EKA2L1_ARM_MEMORY);
if (![-1,0,1].includes(armMemory)) throw Error('Invalid ARM memory policy');
const appUid = process.env.EKA2L1_APP_UID || '0x2000730f';
if (!/^0x[0-9a-fA-F]{1,8}$/.test(appUid) || Number(appUid) === 0) throw Error('Invalid application UID');
const sharedAudio = process.env.EKA2L1_SHARED_AUDIO === "1";
const monitor = process.env.EKA2L1_LONG_MONITOR === "1";
const monitorCpuStart = Number(process.env.EKA2L1_MONITOR_CPU_START_US || '0');
if (!Number.isSafeInteger(monitorCpuStart) || monitorCpuStart < 0 || monitorCpuStart >= endUs || (monitorCpuStart && (!monitor || sampling)))
  throw new Error('Monitor CPU start requires monitoring, no whole-window sampling, and a time before the endpoint');
if (endUs > 120000000 && !monitor) throw new Error("Runs beyond 120 guest seconds require EKA2L1_LONG_MONITOR=1 to discard audio artifacts");
const detailedProfile = process.env.EKA2L1_PROFILE_DETAIL !== '0';
if (!detailedProfile && guestProfile) throw new Error('Guest profiling requires detailed counters');
const hardwareGpu = process.env.EKA2L1_GPU === 'hardware';
const glDiagnostics = process.env.EKA2L1_GL_DIAGNOSTICS === "1";
const aotDiagnostics = process.env.EKA2L1_AOT_DIAGNOSTICS === "1";
const tlbHash = process.env.EKA2L1_TLB_HASH === undefined ? -1 : Number(process.env.EKA2L1_TLB_HASH);
if (![-1,0,1].includes(tlbHash)) throw new Error('Invalid TLB index policy');
const codeWriteProtect = process.env.EKA2L1_CODE_WRITE_PROTECT === undefined ? -1 : Number(process.env.EKA2L1_CODE_WRITE_PROTECT);
if (![-1,0,1].includes(codeWriteProtect)) throw new Error('Invalid code write protection policy');
const codeLookup = process.env.EKA2L1_CODE_LOOKUP === undefined ? -1 : Number(process.env.EKA2L1_CODE_LOOKUP);
if (![-1,0,1].includes(codeLookup)) throw new Error('Invalid code lookup policy');
const omitGuardText=process.env.EKA2L1_OMIT_GUARD_PUBLICATION;
if(omitGuardText!==undefined && !/^[01]$/.test(omitGuardText))throw Error('Invalid guard publication policy');
const omitGuardPublication=omitGuardText===undefined?-1:Number(omitGuardText);
const codeCompare = process.env.EKA2L1_CODE_COMPARE === undefined ? -1 : Number(process.env.EKA2L1_CODE_COMPARE);
if (![-1,0,1,2,3,4].includes(codeCompare)) throw new Error('Invalid exact comparison policy');
const unsafeText=process.env.EKA2L1_UNSAFE_CODE ?? '3';
if(!/^[0123]$/.test(unsafeText))throw Error('Invalid unsafe code mode');
const unsafeCode=Number(unsafeText);
if(![0,1,2,3].includes(unsafeCode))throw Error('Invalid unsafe code mode');
const leafFeatures=Number(process.env.EKA2L1_LEAF_FEATURES || '0');
if(!Number.isInteger(leafFeatures) || leafFeatures<0 || leafFeatures>255)throw Error('Invalid leaf feature mask');
const predicatedLeaves = Number(process.env.EKA2L1_PREDICATED_LEAVES || '0');
if(![0,1].includes(predicatedLeaves))throw Error('Invalid leaf predication setting');
const limitsText = process.env.EKA2L1_EXECUTION_LIMITS || '512,16,8,512';
const executionLimits = limitsText.split(',').map(Number);
if (!/^\d+,\d+,\d+,\d+$/.test(limitsText) || executionLimits.length!==4 || executionLimits.some(n=>!Number.isSafeInteger(n))
    || executionLimits[0]<128 || executionLimits[0]>2048 || executionLimits[0]%4 || executionLimits[1]<1 || executionLimits[1]>64
    || executionLimits[2]<0 || executionLimits[2]>16 || executionLimits[3]<0 || executionLimits[3]>4096) throw new Error('Invalid execution limits');
const irMode = process.env.EKA2L1_AOT_IR_MODE === undefined ? -1 : Number(process.env.EKA2L1_AOT_IR_MODE);
if (![-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16].includes(irMode)) throw new Error('IR mode must be -1 (configured), 0 (disabled), 1 (inline) or 2 (outlined) or 3 (exit recipes) or 4 (invariant reads without IR) or 5 (invariant reads and writes without IR) or 6 (read proofs and budget chunks) or 7 (write proofs and budget chunks) or 8 (deferred chunk counts) or 9 (IR with invariant read proofs) or 10 (IR flags with read proofs) or 11 (IR through inline leaves) or 12 (IR with read/write proofs) or 13 (conditional integer values) or 14 (longer bounded IR segments) or 15 (single-use pure stack values) or 16 (IR with budget chunks in gaps)');
const romCalls = process.env.EKA2L1_ROM_CALLS === undefined ? -1 : Number(process.env.EKA2L1_ROM_CALLS);
if (![-1,0,1].includes(romCalls)) throw new Error('ROM calls must be 0 or 1');
const romLeaves = process.env.EKA2L1_ROM_LEAVES === undefined ? -1 : Number(process.env.EKA2L1_ROM_LEAVES);
if (![-1,0,1].includes(romLeaves)) throw new Error('ROM leaves must be 0 or 1');
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
if (!Number.isInteger(startUs) || startUs < 0 || startUs > 1800000000) throw new Error('Invalid start time');
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
const gitHead = execFileSync('git', ['rev-parse', 'HEAD'], {encoding: 'utf8'}).trim();
const dirtyWorktree = !!execFileSync('git', ['status', '--porcelain'], {encoding: 'utf8'}).trim();
fs.mkdirSync(output); // Refuse to mix captures from different runs.
fs.writeFileSync(path.join(output, 'v8-flags.json'), JSON.stringify({flags: process.env.EKA2L1_V8_FLAGS || ''}));
const files: Record<string,string> = {'/preload/input': input};
for (const name of Object.keys(expected)) files[`/preload/${name}`] = path.join(assets, name);
const {server, port} = await startServer(0, files);
const log = fs.createWriteStream(path.join(output, 'browser.log'));
let browser;
const terminate = async () => {
  await browser?.close();
  server.close();
  log.end();
  process.exit(1);
};
process.once('SIGTERM', terminate);
process.once('SIGINT', terminate);
try {
  browser = await puppeteer.launch({
    executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium',
    headless: true,
    protocolTimeout: 1800000,
    dumpio: process.env.EKA2L1_V8_DUMP === '1',
    args: [...(process.env.EKA2L1_V8_FLAGS ? [`--js-flags=${process.env.EKA2L1_V8_FLAGS}`] : []), '--no-sandbox', '--disable-dev-shm-usage', '--use-gl=angle', ...(hardwareGpu ? ['--use-angle=vulkan', '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist'] : ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']), '--disable-background-timer-throttling'],
  });
  const system = await browser.target().createCDPSession();
  const gpuInfo = await system.send('SystemInfo.getInfo');
  fs.writeFileSync(path.join(output, 'gpu.json'), JSON.stringify(gpuInfo.gpu, null, 2));
  const page = await browser.newPage();
  const failures: string[] = [];
  page.on('console', msg => {log.write(`${msg.type()}: ${msg.text()}\n`); if (msg.text().includes('ABORT:')) failures.push(msg.text());});
  page.on('pageerror', error => failures.push(String(error)));
  page.on('requestfailed', request => failures.push(`${request.url()}: ${request.failure()?.errorText}`));
  page.on('response', response => {if (response.status() >= 400) failures.push(`HTTP ${response.status()} ${response.url()}`);});
  await page.goto(`http://127.0.0.1:${port}/`, {waitUntil: 'domcontentloaded'});
  await page.waitForFunction(() => (window as any).Module?.calledRun, {timeout: 120000});
  const glDiagnosticsSupported = await page.evaluate(() => typeof (window as any).Module._eka2l1_graphics_diagnostics_configure === 'function');
  await page.evaluate(async ({romCalls, romLeaves, thumbMemory, armMemory, appUid, tlbHash, codeCompare, codeLookup, omitGuardPublication, codeWriteProtect, eagerRegions, irMode, predicatedLeaves, leafFeatures, unsafeCode, executionLimits, count, startUs, captureMode, endUs, aot, verifyAot, aotDiagnostics, guestProfile, exitCensus, glDiagnostics, detailedProfile, monitor, sharedAudio}) => {
    const g = window as any;
    const call = (name: string, types: string[], args: unknown[]) => {
      const code = g.Module.ccall(name, 'number', types, args);
      if (code !== 0) throw new Error(`${name} returned ${code}`);
    };
    if (monitor) call('eka2l1_monitor_configure', [], []);
    if (!detailedProfile) call('eka2l1_profile_detail_configure', ['number'], [0]);
    call('eka2l1_profile_configure', ['number', 'number', 'number'], [startUs, endUs, captureMode]);
    call('eka2l1_benchmark_configure', ['number', 'number', 'number'], [count, startUs, 1]);
    if (sharedAudio) call('eka2l1_audio_configure', [], []);
    call('eka2l1_aot_configure', ['number', 'number', 'number'], [aot, verifyAot, aotDiagnostics ? 1 : 0]);
    if (romCalls !== -1) call('eka2l1_rom_calls_configure', ['number'], [romCalls]);
    g.romCallsActual = typeof g.Module._eka2l1_rom_calls_report === 'function'
      ? g.Module._eka2l1_rom_calls_report() : null;
    if (romCalls !== -1 && g.romCallsActual !== romCalls) throw Error('ROM calls readback mismatch');
    if (romLeaves !== -1) {
      call('eka2l1_rom_leaves_configure', ['number'], [romLeaves]);
    }
    g.romLeavesActual = typeof g.Module._eka2l1_rom_leaves_report === 'function'
      ? g.Module._eka2l1_rom_leaves_report() : null;
    if (romLeaves !== -1 && g.romLeavesActual !== romLeaves) throw Error('ROM leaves readback mismatch');
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
    if (omitGuardPublication !== -1) {
      if (typeof g.Module._eka2l1_omit_guard_publication_configure !== 'function') throw Error('Build lacks guard publication selection');
      call('eka2l1_omit_guard_publication_configure',['number'],[omitGuardPublication]);
    }
    g.omitGuardPublicationActual=typeof g.Module._eka2l1_omit_guard_publication_report==='function'
      ?g.Module.ccall('eka2l1_omit_guard_publication_report','number',[],[]):null;
    if(omitGuardPublication!==-1 && g.omitGuardPublicationActual!==omitGuardPublication)throw Error('Guard publication readback mismatch');
    if (codeCompare !== -1) {
      if (typeof g.Module._eka2l1_code_compare_configure !== 'function') throw new Error('Build lacks exact comparison selection');
      call('eka2l1_code_compare_configure', ['number'], [codeCompare]);
    }
    if (irMode !== -1) {
      if (typeof g.Module._eka2l1_ir_configure !== 'function') throw new Error('Build lacks IR mode selection');
      call('eka2l1_ir_configure', ['number'], [irMode]);
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
    if (typeof g.Module._eka2l1_execution_limits_configure === 'function') {
      call('eka2l1_execution_limits_configure', ['number','number','number','number'], executionLimits);
      const observed=g.Module.ccall('eka2l1_execution_limits_report','string',[],[]);
      if(observed!==executionLimits.join(','))throw new Error('Execution limits were not applied');
    } else if(executionLimits.join(',')!=='512,16,8,512')throw new Error('Execution limits API unavailable');
    call('eka2l1_guest_profile_configure', ['number'], [guestProfile]);
    if(exitCensus) call('eka2l1_exit_census_configure', ['number'], [1]);
    if (typeof g.Module._eka2l1_graphics_diagnostics_configure === 'function')
      call('eka2l1_graphics_diagnostics_configure', ['number'], [glDiagnostics ? 1 : 0]);
    else if (glDiagnostics) throw new Error('Build does not support graphics diagnostic configuration');
    if (thumbMemory !== -1) {
      call('eka2l1_thumb_memory_configure', ['number'], [thumbMemory]);
      if (g.Module.ccall('eka2l1_thumb_memory_report', 'number', [], []) !== thumbMemory)
        throw Error('Thumb memory policy readback mismatch');
    }
    if (armMemory !== -1) {
      call('eka2l1_arm_memory_configure', ['number'], [armMemory]);
      if (g.Module.ccall('eka2l1_arm_memory_report', 'number', [], []) !== armMemory)
        throw Error('ARM memory policy readback mismatch');
    }
    call('eka2l1_init', ['string'], ['/data']);
    if (thumbMemory !== -1 && g.Module._eka2l1_thumb_memory_configure(1 - thumbMemory) !== -1)
      throw Error('Thumb memory policy changed after initialization');
    if (romCalls !== -1 && g.Module._eka2l1_rom_calls_configure(1 - romCalls) !== -1)
      throw Error('ROM calls changed after initialization');
    if (romLeaves !== -1 && g.Module._eka2l1_rom_leaves_configure(1 - romLeaves) !== -1)
      throw Error('ROM leaves changed after initialization');
    if (armMemory !== -1 && g.Module._eka2l1_arm_memory_configure(1 - armMemory) !== -1)
      throw Error('ARM memory policy changed after initialization');
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
    call('eka2l1_run', ['string'], [appUid]);
  }, {romCalls, romLeaves, thumbMemory, armMemory, appUid, tlbHash, codeCompare, codeLookup, omitGuardPublication, codeWriteProtect, eagerRegions, irMode, predicatedLeaves, leafFeatures, unsafeCode, executionLimits, count: frames, startUs, captureMode, endUs, aot, verifyAot, aotDiagnostics, guestProfile, exitCensus, glDiagnostics, detailedProfile, monitor, sharedAudio});
  async function waitPhase(phase: number) {
    const deadline = performance.now() + 1800000;
    while (await page.evaluate(() => (window as any).Module._eka2l1_profile_phase()) !== phase) {
      if (failures.length) throw new Error(failures.join('\n'));
      if (performance.now() > deadline) throw new Error(`Profile phase ${phase} timeout`);
      await new Promise(resolve => setTimeout(resolve, 250));
    }
  }
  const warmupStart = performance.now();
  await waitPhase(1);
  const warmupSeconds = (performance.now() - warmupStart) / 1000;
  if (process.env.PROFILE_GATE) {
    fs.writeFileSync(`${process.env.PROFILE_GATE}.ready`, JSON.stringify({warmup_seconds: warmupSeconds}));
    const deadline = performance.now() + 1800000;
    while (!fs.existsSync(process.env.PROFILE_GATE)) {
      if (failures.length) throw new Error(failures.join('\n'));
      if (performance.now() > deadline) throw new Error('Profile gate timeout');
      await new Promise(resolve => setTimeout(resolve, 250));
    }
  }
  const clients = [{name: 'page' , client: await page.createCDPSession()},
    ...page.workers().map((worker, i) => ({name: `worker-${i}`, client: (worker as any).client}))];
  console.log(`Warmup ${warmupSeconds.toFixed(3)}s; profiling ${clients.length} isolates; mode ${captureMode}`);
  if (sampling) await Promise.all(clients.map(async ({client}) => {
    await client.send('Profiler.enable');
    await client.send('Profiler.setSamplingInterval', {interval: sampleInterval});
    await client.send('Profiler.start');
  }));
  await page.evaluate(() => (window as any).Module._eka2l1_profile_resume());
  const timeline: unknown[] = [];
  let monitorCpuStarted = false;
  if (monitor) {
    let lastScene = -1;
    const deadline = performance.now() + 1800000;
    while (true) {
      const before = performance.now();
      const sample = await page.evaluate(() => {
        const g = window as any;
        return {...JSON.parse(g.Module.ccall('eka2l1_monitor_report', 'string', [], [])),
          worker_pool: {running: g.PThread?.runningWorkers?.length, unused: g.PThread?.unusedWorkers?.length},
          phase: g.Module._eka2l1_profile_phase(), presentations: g.Module._eka2l1_presentations(),
          linear_bytes: g.HEAPU8?.buffer.byteLength ?? g.Module.HEAPU8?.buffer.byteLength};
      });
      if (monitorCpuStart && !monitorCpuStarted && sample.guest_us >= monitorCpuStart && sample.phase !== 3) {
        await Promise.all(clients.map(async ({client}) => {
          await client.send('Profiler.enable');
          await client.send('Profiler.setSamplingInterval', {interval: sampleInterval});
          await client.send('Profiler.start');
        }));
        monitorCpuStarted = true;
        fs.writeFileSync(path.join(output, 'cpu-window.json'), JSON.stringify({first_guest_us: sample.guest_us}));
      }
      const heaps = [{name: 'page', ...await clients[0].client.send('Runtime.getHeapUsage')}];
      const processes = await system.send('SystemInfo.getProcessInfo');
      let pssKiB = 0;
      for (const proc of processes.processInfo) {
        try { pssKiB += Number(fs.readFileSync(`/proc/${proc.id}/smaps_rollup`, 'utf8').match(/^Pss:\s+(\d+)/m)?.[1] ?? 0); } catch {}
      }
      timeline.push({...sample, host_ms: before, probe_ms: performance.now() - before, heaps, pss_kib: pssKiB});
      fs.writeFileSync(path.join(output, 'timeline.json'), JSON.stringify(timeline));
      const scene = Math.floor(sample.guest_us / 60000000);
      if (scene !== lastScene) {
        lastScene = scene;
        await page.screenshot({path: path.join(output, `scene-${scene}.png`)});
      }
      if (sample.phase === 3) break;
      if (failures.length) throw new Error(failures.join('\n'));
      if (performance.now() > deadline) throw new Error('Long monitor timeout');
      await new Promise(resolve => setTimeout(resolve, 2000));
    }
  } else await waitPhase(3);
  const measured = await page.evaluate(() => JSON.parse((window as any).Module.ccall('eka2l1_profile_report', 'string', [], [])));
  console.log(JSON.stringify(measured));
  if (guestProfile) {
    const guest = await page.evaluate(() => JSON.parse((window as any).Module.ccall('eka2l1_guest_profile_report', 'string', [], [])));
    fs.writeFileSync(path.join(output, 'guest-profile.json'), JSON.stringify(guest));
  }
  if (sampling || monitorCpuStarted) await Promise.all(clients.map(async ({name, client}) => {
    const {profile} = await client.send('Profiler.stop');
    fs.writeFileSync(path.join(output, `${name}.cpuprofile`), JSON.stringify(profile));
  }));
  let captureWorker = process.env.EKA2L1_CAPTURE_MODULES;
  if (captureWorker === 'auto') {
    if (!sampling) throw new Error('Automatic module capture requires CPU sampling');
    const ranked = clients.map(({name}) => {
      const profile = JSON.parse(fs.readFileSync(path.join(output, `${name}.cpuprofile`), 'utf8'));
      const generated = new Set(profile.nodes.filter((n: any) => n.callFrame.url.startsWith('wasm://')).map((n: any) => n.id));
      const weight = profile.samples.reduce((sum: number, id: number, i: number) => sum + (generated.has(id) ? profile.timeDeltas[i] : 0), 0);
      return {name, weight};
    }).sort((a,b) => b.weight - a.weight);
    if (!ranked[0]?.weight) throw new Error('No sampled generated-code worker for module capture');
    captureWorker = ranked[0].name;
    fs.writeFileSync(path.join(output, 'capture-worker.json'), JSON.stringify({selected: captureWorker, ranked}));
  }
  for (const {client,name} of clients.filter(c => c.name === captureWorker)) {
    const captured = await client.send('Runtime.evaluate', {expression:'JSON.stringify(globalThis.ekaProbeModules || [])',returnByValue:true}, {timeout:10000});
    for (const mod of JSON.parse(captured.result.value || '[]')) {
      const base = `${name}-module-${mod.index}`;
      if (mod.base64) fs.writeFileSync(path.join(output,base+'.wasm'),Buffer.from(mod.base64,'base64'));
      delete mod.base64;fs.writeFileSync(path.join(output,base+'.json'),JSON.stringify(mod));
    }
  }
  if (process.env.EKA2L1_COMPILE_CENSUS === '1') {
    const modules = await page.evaluate(() => (window as any).FS.readdir('/tmp').filter((n: string) => /^census-module-.*\.wasm$/.test(n)));
    fs.mkdirSync(path.join(output, 'modules'));
    for (const name of modules) {
      const data = await page.evaluate((n) => {
        const bytes = (window as any).FS.readFile('/tmp/' + n);
        let text = ''; for(let i=0;i<bytes.length;i+=8192) text += String.fromCharCode(...bytes.subarray(i,i+8192));
        return btoa(text);
      }, name);
      fs.writeFileSync(path.join(output, 'modules', name), Buffer.from(data, 'base64'));
    }
  }
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
  await page.screenshot({path: path.join(output, 'browser.png')});
  if (failures.length) throw new Error(failures.join('\n'));
  if (omitGuardPublication !== -1) await page.evaluate(() => {
    const g=window as any;
    if(g.Module.ccall('eka2l1_omit_guard_publication_configure','number',['number'],[1-g.omitGuardPublicationActual])!==-1)
      throw Error('Guard publication changed after CPU initialization');
    if(g.Module.ccall('eka2l1_omit_guard_publication_report','number',[],[])!==g.omitGuardPublicationActual)
      throw Error('Guard publication readback changed');
  });
  fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({rom_calls: await page.evaluate(() => (globalThis as any).romCallsActual), rom_leaves: await page.evaluate(() => (globalThis as any).romLeavesActual), thumb_memory: thumbMemory, arm_memory: armMemory, app_uid: appUid, measurement: measured, warmup_seconds: warmupSeconds,
    shared_audio: sharedAudio, guest_profile_stride: guestProfile, exit_census:exitCensus, monitor, monitor_cpu_start_us: monitorCpuStart, sampling, sample_interval_us: sampleInterval, isolates: clients.length, assets: expected, input_sha256: inputHash, wasm_sha256: wasmHash, loader_sha256: loaderHash,
    gl_diagnostics: glDiagnostics || !glDiagnosticsSupported, gl_diagnostics_configurable: glDiagnosticsSupported,
    aot, aot_diagnostics: aotDiagnostics, ir_mode: irMode, execution_limits:executionLimits, predicated_leaves:predicatedLeaves, leaf_features:leafFeatures, unsafe_code_initial:await page.evaluate(() => (globalThis as any).unsafeCodeInitial ?? null), unsafe_code:await page.evaluate(() => (globalThis as any).unsafeCodeActual), tlb_hash: tlbHash, code_compare: codeCompare, code_lookup: codeLookup, omit_guard_publication:await page.evaluate(()=>(globalThis as any).omitGuardPublicationActual), code_write_protect: codeWriteProtect, eager_regions: eagerRegions, verify_aot: verifyAot, git_head: gitHead, dirty_worktree: dirtyWorktree, browser: await browser.version(),
    runtime_footprint: await page.evaluate(() => {
      const m=(window as any).Module;
      return typeof m._eka2l1_monitor_report==='function' ? JSON.parse(m.ccall('eka2l1_monitor_report','string',[],[])) : null;
    }),
    user_agent: await page.evaluate(() => navigator.userAgent),
    renderer: await page.evaluate(() => {
      const gl = document.createElement('canvas').getContext('webgl2');
      const ext = gl?.getExtension('WEBGL_debug_renderer_info');
      return ext ? gl!.getParameter(ext.UNMASKED_RENDERER_WEBGL) : 'unavailable';
    })}, null, 2));
  console.log('PASS: captured performance profile');
} finally {
  process.removeListener('SIGTERM', terminate);
  process.removeListener('SIGINT', terminate);
  await browser?.close();
  server.close();
  log.end();
}
