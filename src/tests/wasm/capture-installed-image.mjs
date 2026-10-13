// Capture a clean N-Gage installation, before launching or playing the game.
// Large firmware/game bytes remain outside Git. The image is installed files,
// with no CPU/kernel/browser checkpoint or cached generated WASM code.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {Readable} from 'node:stream';
import {pipeline} from 'node:stream/promises';
import puppeteer from 'puppeteer';

const [url, game, output, assetDir = '/tmp/eka2l1-serve'] = process.argv.slice(2);
const entry = JSON.parse(fs.readFileSync(new URL('./games.json', import.meta.url)))
  .find(entry => entry.id === game);
if (!url || !output || !entry?.ngage || entry.device)
  throw Error('Usage: node capture-installed-image.mjs URL STOCK_DEVICE_NGAGE_GAME NEW_OUTPUT [ASSET_DIR]');
fs.mkdirSync(output);
const tree = path.join(output, 'files');
fs.mkdirSync(tree);
const sources = {
  rom:'89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490',
  rpkg:'58964f3d08a542f01118a7dfb78a34d2e029962b8edb9988381a37994c1c1531',
  game:entry.ngage.sha256, runtime:entry.ngage.runtime.sha256,
};
const browser = await puppeteer.launch({executablePath:'/usr/bin/chromium', headless:true,
  args:['--host-resolver-rules=MAP claude-laptop.lan 127.0.0.1', '--no-sandbox',
    '--disable-dev-shm-usage', '--use-gl=angle', '--use-angle=vulkan',
    '--enable-features=Vulkan', '--enable-gpu', '--ignore-gpu-blocklist',
    '--disable-background-timer-throttling']});
try {
  const page = await browser.newPage();
  await page.setViewport({width:900,height:800});
  page.setDefaultTimeout(180000);
  let complete = false;
  const errors = [];
  page.on('console', message => {
    const text = message.text();
    fs.appendFileSync(path.join(output,'browser.log'), text + '\n');
    if (text.includes('Thread N-Gage Installer forcefully killed with category: None and exit code: 0'))
      complete = true;
    if (/Access violation|KERN-EXEC|ABORT:|panicked with category:/.test(text)) errors.push(text);
  });
  page.on('pageerror', error => errors.push(String(error)));
  const target = new URL(url); target.searchParams.set('game','custom');
  await page.goto(target.href, {waitUntil:'domcontentloaded'});
  await page.waitForFunction(() => Module.calledRun);
  await page.evaluate(() => {
    if (Module._eka2l1_route_configure(1000000000) !== 0) throw Error('Route configuration failed');
  });
  for (const [selector, file, expected] of [
    ['#rom-file','SYM.ROM',sources.rom], ['#rpkg-file','SYM.RPKG',sources.rpkg],
    ['#sis-file',entry.ngage.runtime.file,sources.runtime],
  ]) {
    const asset = path.join(assetDir,file);
    const hash = crypto.createHash('sha256');
    for await (const bytes of fs.createReadStream(asset)) hash.update(bytes);
    if (hash.digest('hex') !== expected) throw Error('Source hash mismatch: ' + file);
    await (await page.$(selector)).uploadFile(asset);
  }
  const packageHash = crypto.createHash('sha256');
  for await (const bytes of fs.createReadStream(path.join(assetDir,entry.ngage.file))) packageHash.update(bytes);
  if (packageHash.digest('hex') !== sources.game) throw Error('Game package hash mismatch');
  await page.$eval('#app-name', (element,uid) => element.value=uid, entry.uid);
  await page.evaluate(id => {
    window.captureLaunch = startEmulator('/preload/games/' + id + '/ngage');
  }, game);
  await page.waitForFunction(() => window._gameRunning && Module._eka2l1_route_phase() === 1);
  if (!complete || errors.length) throw Error('Clean installation not completed: ' + JSON.stringify(errors));
  await page.screenshot({path:path.join(output,'installation-complete.png')});
  // Close guest file handles and save registries before reading the files.
  await page.evaluate(() => Module.ccall('eka2l1_shutdown', null, [], []));
  const entries = await page.evaluate(() => {
    const result = [];
    function visit(root) {
      for (const name of FS.readdir(root).filter(name => name !== '.' && name !== '..').sort()) {
        const path = root + '/' + name, stat = FS.stat(path);
        if ((stat.mode & 0xf000) === 0x4000) {
          result.push({path,type:'directory',size:0}); visit(path);
        } else if ((stat.mode & 0xf000) === 0x8000) {
          result.push({path,type:'file',size:stat.size});
        } else throw Error('Unsupported filesystem entry: ' + path);
      }
    }
    visit('/data'); return result;
  });
  await page.exposeFunction('saveInstalledChunk', (relative, encoded) => {
    const destination = path.join(tree,relative.slice('/data/'.length));
    fs.mkdirSync(path.dirname(destination), {recursive:true});
    fs.appendFileSync(destination,Buffer.from(encoded,'base64'));
  });
  await page.evaluate(async entries => {
    for (const entry of entries) {
      if (entry.type !== 'file') continue;
      const bytes = FS.readFile(entry.path);
      if (bytes.length !== entry.size) throw Error('File changed during capture');
      if (!bytes.length) await window.saveInstalledChunk(entry.path,'');
      for (let start=0; start<bytes.length; start+=1024*1024) {
        const chunk=bytes.subarray(start,start+1024*1024);
        let binary='';
        for (let i=0; i<chunk.length; i+=32768)
          binary+=String.fromCharCode(...chunk.subarray(i,i+32768));
        await window.saveInstalledChunk(entry.path,btoa(binary));
      }
    }
  }, entries);
  const header = Buffer.from(JSON.stringify({version:1,game,sources,entries}));
  const prefix = Buffer.alloc(4); prefix.writeUInt32LE(header.length);
  async function* contents() {
    yield prefix; yield header;
    for (const entry of entries) {
      if (entry.type !== 'file') continue;
      yield* fs.createReadStream(path.join(tree,entry.path.slice('/data/'.length)));
    }
  }
  const archive = path.join(output,game+'.img.gz');
  await pipeline(Readable.from(contents()),zlib.createGzip({level:6}),fs.createWriteStream(archive));
  const hash = crypto.createHash('sha256');
  for await (const bytes of fs.createReadStream(archive)) hash.update(bytes);
  const metadata = {version:1,game,file:path.basename(archive),sources,sha256:hash.digest('hex'),
    files:entries.filter(entry => entry.type==='file').length,
    unpackedBytes:entries.reduce((total,entry)=>total+entry.size,0),
    compressedBytes:fs.statSync(archive).size, browser:await browser.version(),
    capture:'Fresh installation; installer completed; CPU parked; shutdown closed file handles; game not launched'};
  fs.writeFileSync(path.join(output,game+'.json'),JSON.stringify(metadata,null,2));
  fs.writeFileSync(path.join(output,'entries.json'),JSON.stringify(entries,null,2));
  console.log(JSON.stringify(metadata));
} finally {
  await browser.close();
}
