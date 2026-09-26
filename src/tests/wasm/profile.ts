import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import puppeteer from 'puppeteer';
import {startServer, buildDir} from './server.ts';

const [assetArg, outputArg, modeArg = '0', samplingArg = '1', endArg = '25000000'] = process.argv.slice(2);
const frameArg = '100000', inputArg = '../benchmark/snakes.input', startArg = '21000000';
const captureMode = Number(modeArg), sampling = samplingArg === '1', endUs = Number(endArg);
if (![0,1,2].includes(captureMode) || !Number.isInteger(endUs) || endUs <= Number(startArg) || endUs > 120000000) throw new Error('Invalid profile settings');
if (!assetArg || !outputArg) throw new Error('Usage: node profile.ts ASSETS NEW_OUTPUT [CAPTURE_MODE:0/1/2] [SAMPLING:0/1] [END_US]');
const aotDiagnostics = process.env.EKA2L1_AOT_DIAGNOSTICS === "1";
const verifyAot = process.env.EKA2L1_AOT_VERIFY === "1";
const aot = Number(process.env.EKA2L1_BENCHMARK_AOT || "0");
if (![0,1,2,3,4].includes(aot)) throw new Error("AOT mode must be 0, 1, 2, 3 or 4");
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
  await page.evaluate(async ({count, startUs, captureMode, endUs, aot, verifyAot, aotDiagnostics}) => {
    const g = window as any;
    const call = (name: string, types: string[], args: unknown[]) => {
      const code = g.Module.ccall(name, 'number', types, args);
      if (code !== 0) throw new Error(`${name} returned ${code}`);
    };
    call('eka2l1_profile_configure', ['number', 'number', 'number'], [startUs, endUs, captureMode]);
    call('eka2l1_benchmark_configure', ['number', 'number', 'number'], [count, startUs, 1]);
    call('eka2l1_aot_configure', ['number', 'number', 'number'], [aot, verifyAot ? 1 : 0, aotDiagnostics ? 1 : 0]);
    call('eka2l1_init', ['string'], ['/data']);
    for (const name of ['SYM.ROM', 'SYM.RPKG', 'Snakes.sis', 'input']) {
      const response = await fetch(`/preload/${name}`);
      if (!response.ok) throw new Error(`Asset HTTP ${response.status()}`);
      g.FS.writeFile(name === 'input' ? '/benchmark.input' : `/tmp/${name}`, new Uint8Array(await response.arrayBuffer()));
    }
    call('eka2l1_install_device', ['string', 'string'], ['/tmp/SYM.ROM', '/tmp/SYM.RPKG']);
    call('eka2l1_install_sis', ['string'], ['/tmp/Snakes.sis']);
    g.FS.mkdir('/frames');
    g.Module.ccall('eka2l1_start_frame_dump', null, ['string', 'number'], ['/frames', count]);
    call('eka2l1_run', ['string'], ['Snakes']);
  }, {count: frames, startUs, captureMode, endUs, aot, verifyAot, aotDiagnostics});
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
    await client.send('Profiler.setSamplingInterval', {interval: 1000});
    await client.send('Profiler.start');
  }));
  await page.evaluate(() => (window as any).Module._eka2l1_profile_resume());
  await waitPhase(3);
  const measured = await page.evaluate(() => JSON.parse((window as any).Module.ccall('eka2l1_profile_report', 'string', [], [])));
  console.log(JSON.stringify(measured));
  if (sampling) await Promise.all(clients.map(async ({name, client}) => {
    const {profile} = await client.send('Profiler.stop');
    fs.writeFileSync(path.join(output, `${name}.cpuprofile`), JSON.stringify(profile));
  }));
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
  fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({measurement: measured, warmup_seconds: warmupSeconds,
    sampling, isolates: clients.length, assets: expected, input_sha256: inputHash, wasm_sha256: wasmHash,
    aot, aot_diagnostics: aotDiagnostics, verify_aot: verifyAot, git_head: gitHead, dirty_worktree: dirtyWorktree, browser: await browser.version(),
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
