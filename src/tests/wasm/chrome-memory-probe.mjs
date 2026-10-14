import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import puppeteer from 'puppeteer';
import {ChromeMemoryProfiler} from './chrome-memory-profiler.mjs';

const [fixture, output] = process.argv.slice(2);
if (!fixture || !output) throw Error('Usage: node chrome-memory-probe.mjs FIXTURE_BUILD NEW_OUTPUT');
fs.mkdirSync(output);
const server = http.createServer((req,res) => {
  const name = new URL(req.url,'http://localhost').pathname.slice(1) || 'memory-profiler-fixture.html';
  if (!/^[\w.-]+$/.test(name) || !fs.existsSync(path.join(fixture,name))) {res.writeHead(404);res.end();return;}
  res.setHeader('Cross-Origin-Opener-Policy','same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
  res.setHeader('Content-Type', name.endsWith('.wasm') ? 'application/wasm' : name.endsWith('.js') ? 'text/javascript' : name.endsWith('.map') ? 'application/json' : 'text/html');
  fs.createReadStream(path.join(fixture,name)).pipe(res);
});
await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
const base = `http://127.0.0.1:${server.address().port}/`;
const browser = await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
try {
  const page = await browser.newPage(), errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.goto(base);
  await page.waitForFunction(() => Module.calledRun);
  const wasm = fs.readFileSync(path.join(fixture,'memory-profiler-fixture.wasm'));
  const map = fs.readFileSync(path.join(fixture,'memory-profiler-fixture.wasm.map'));
  const mapResponse = await page.evaluate(async url => {const r=await fetch(url);return {status:r.status,body:await r.text()};},base+'memory-profiler-fixture.wasm.map');
  assert.equal(mapResponse.status,200);
  assert.equal(mapResponse.body,map.toString());
  fs.writeFileSync(path.join(output,'fixture.wasm.map'),map);
  fs.writeFileSync(path.join(output,'fixture.wasm'),wasm);
  const status = () => page.evaluate(() => ({cpp_allocated:Module._allocated_cpp_bytes(),
    wasm_capacity:HEAPU8.buffer.byteLength, shared:HEAPU8.buffer instanceof SharedArrayBuffer}));
  const results = [];
  for (const stage of ['cpp-malloc-32mib','js-array-16mib','cpp-to-js-array-16mib']) {
    const stageOutput = path.join(output,stage);fs.mkdirSync(stageOutput);
    const profiler = new ChromeMemoryProfiler(page,stageOutput);
    await profiler.start();
    const before = await status();
    const allocation = await page.evaluate(stage => {
      if (stage === 'cpp-malloc-32mib') return Module._allocate_cpp_control(32*1024*1024);
      if (stage === 'cpp-to-js-array-16mib') return Module._allocate_js_from_cpp(2*1024*1024);
      function allocateJavaScriptControl() {globalThis.js_array = new Array(2*1024*1024).fill(0.5);}
      allocateJavaScriptControl();
    },stage);
    if (stage === 'cpp-malloc-32mib') assert.ok(allocation);
    const after = await status(), profiles = await profiler.stop();
    if (stage === 'cpp-malloc-32mib') {
      assert.ok(after.cpp_allocated-before.cpp_allocated >= 32*1024*1024);
      assert.equal(after.wasm_capacity,before.wasm_capacity,'Control must suballocate inside existing WASM memory');
    } else assert.ok(profiles[0].v8.sampled_bytes > 4*1024*1024,'V8 profiler must observe the JS positive control');
    results.push({stage,before,after,profiles});
  }
  // A heap snapshot tests object ownership/backing visibility separately from
  // allocation-stack sampling. Snapshotting is intentionally outside timing.
  const client = await page.createCDPSession();
  const snapshot = fs.createWriteStream(path.join(output,'fixture.heapsnapshot'));
  client.on('HeapProfiler.addHeapSnapshotChunk',({chunk}) => snapshot.write(chunk));
  await client.send('HeapProfiler.enable');
  await client.send('HeapProfiler.takeHeapSnapshot',{reportProgress:false});
  await new Promise(resolve => snapshot.end(resolve));
  await page.evaluate(() => Module._release_cpp_control());
  assert.equal(errors.length,0);
  const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({browser:await browser.version(),base,
    wasm_sha256:hash(wasm),source_map_sha256:hash(map),source_map_served:true,
    source_map_sources:JSON.parse(map).sources,errors,results},null,2));
  console.log(JSON.stringify(results.map(r => ({stage:r.stage,cpp_delta:r.after.cpp_allocated-r.before.cpp_allocated,
    capacity_delta:r.after.wasm_capacity-r.before.wasm_capacity,profiles:r.profiles.map(p=>({target:p.target,
      v8_sampled_bytes:p.v8?.sampled_bytes,wasm_frames:p.v8?.wasm_frames.length,native_samples:p.native?.samples,errors:p.errors}))})),null,2));
} finally {
  await browser.close();
  await new Promise(resolve => server.close(resolve));
}
