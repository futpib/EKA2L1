import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import puppeteer from 'puppeteer';

const [directory, output, limitText = '128'] = process.argv.slice(2);
const limit = Number(limitText);
if (!directory || !output || !Number.isSafeInteger(limit) || limit < 1)
  throw Error('Usage: node module-handoff-fixture.mjs MODULE_DIRECTORY OUTPUT.json [COUNT]');

const files = fs.readdirSync(directory).filter(name => /^census-module-\d+\.wasm$/.test(name))
  .sort((a, b) => Number(a.match(/\d+/)[0]) - Number(b.match(/\d+/)[0])).slice(0, limit);
if (!files.length) throw Error('No captured generated WASM modules in ' + directory);

const server = http.createServer((request, response) => {
  const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname.slice(1));
  if (!files.includes(name)) {
    response.writeHead(name ? 404 : 200, {'Content-Type': 'text/html'}).end(name ? 'Not found' : '<title>Module handoff</title>');
    return;
  }
  response.writeHead(200, {'Content-Type': 'application/wasm'});
  fs.createReadStream(path.join(directory, name)).pipe(response);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));

const workerSource = `
  const memory = new WebAssembly.Memory({initial:256, maximum:65536, shared:true});
  const noop = () => 0;
  const imports = {env:{memory, tlb_read32:noop, tlb_write32:noop,
    tlb_read8:noop, tlb_write8:noop, tlb_read16:noop, tlb_write16:noop,
    exclusive_read32:noop, exclusive_write32:noop}};
  onmessage = ({data}) => {
    try {
      const start = performance.now();
      const modules = data.modules || data.bytes.map(bytes => new WebAssembly.Module(bytes));
      const compiled = performance.now();
      let exports = 0;
      for (const module of modules)
        exports += Object.keys(new WebAssembly.Instance(module, imports).exports).length;
      const instantiated = performance.now();
      postMessage({compileMs:compiled-start, instantiateMs:instantiated-compiled,
        exports, modules:data.returnModules ? modules : undefined});
    } catch (error) { postMessage({error:String(error)}); }
  };
`;

const browser = await puppeteer.launch({executablePath:'/usr/bin/chromium', headless:true,
  args:['--no-sandbox','--disable-dev-shm-usage']});
const system = await browser.target().createCDPSession();
const page = await browser.newPage();
const client = await page.createCDPSession();
const memory = async () => {
  const rows = (await system.send('SystemInfo.getProcessInfo')).processInfo;
  let rendererPssKiB = 0, wholeBrowserPssKiB = 0;
  for (const row of rows) {
    try {
      const rollup = fs.readFileSync(`/proc/${row.id}/smaps_rollup`, 'utf8');
      const pss = Number(rollup.match(/^Pss:\s+(\d+)/m)?.[1] ?? 0);
      wholeBrowserPssKiB += pss;
      if (row.type === 'renderer') rendererPssKiB += pss;
    } catch {}
  }
  return {rendererPssKiB, wholeBrowserPssKiB};
};
const collect = async () => {
  await client.send('HeapProfiler.collectGarbage');
  await new Promise(resolve => setTimeout(resolve, 500));
};

try {
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.evaluate(async ({files, workerSource}) => {
    const bytes = await Promise.all(files.map(async name => (await fetch('/' + name)).arrayBuffer()));
    const url = URL.createObjectURL(new Blob([workerSource], {type:'text/javascript'}));
    globalThis.fixture = {bytes, url, modules:null};
    globalThis.runFixtureWorker = (kind) => new Promise((resolve, reject) => {
      const worker = new Worker(url);
      const started = performance.now();
      worker.onerror = event => { worker.terminate(); reject(Error(event.message)); };
      worker.onmessage = event => {
        worker.terminate();
        if (event.data.error) { reject(Error(event.data.error)); return; }
        if (kind === 'capture') globalThis.fixture.modules = event.data.modules;
        const {modules, ...times} = event.data;
        resolve({...times, roundTripMs:performance.now()-started});
      };
      worker.postMessage(kind === 'reuse'
        ? {modules:globalThis.fixture.modules}
        : {bytes:globalThis.fixture.bytes, returnModules:kind === 'capture'});
    });
  }, {files, workerSource});

  const baseline = await memory();
  const capture = await page.evaluate(() => runFixtureWorker('capture'));
  await collect();
  const held = await memory();
  const reuse = await page.evaluate(() => runFixtureWorker('reuse'));
  await collect();
  const reused = await memory();
  const fresh = await page.evaluate(() => runFixtureWorker('fresh'));
  await collect();
  const rebuilt = await memory();
  await page.evaluate(() => { fixture.modules = null; });
  await collect();
  const modulesReleased = await memory();
  await page.evaluate(() => { fixture.bytes = null; });
  await collect();
  const allReleased = await memory();
  const report = {browser:await browser.version(), sourceBytes:files.reduce((sum, name) =>
    sum + fs.statSync(path.join(directory, name)).size, 0), count:files.length,
    baseline, capture, held, reuse, reused, fresh, rebuilt, modulesReleased, allReleased};
  fs.writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
  if (capture.exports !== reuse.exports || capture.exports !== fresh.exports)
    throw Error('Export counts did not match across workers');
} finally {
  await browser.close();
  await new Promise(resolve => server.close(resolve));
}
