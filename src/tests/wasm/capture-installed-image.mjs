// Capture a clean SIS or N-Gage installation, before launching the game.
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
if (!url || !output || !entry)
  throw Error('Usage: node capture-installed-image.mjs URL STOCK_DEVICE_GAME NEW_OUTPUT [ASSET_DIR]');
fs.mkdirSync(output);
const tree = path.join(output, 'files');
fs.mkdirSync(tree);
const sources = {
  rom:'89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490',
  rpkg:'58964f3d08a542f01118a7dfb78a34d2e029962b8edb9988381a37994c1c1531',
  game:entry.ngage?.sha256 || entry.sha256,
  ...(entry.ngage ? {runtime:entry.ngage.runtime.sha256} : {}),
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
  await page.evaluateOnNewDocument(() => { window.ekaTransientSession = true; });
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
  for (const [selector, file, expected] of [
    ['#rom-file','SYM.ROM',sources.rom], ['#rpkg-file','SYM.RPKG',sources.rpkg],
    ['#sis-file',entry.ngage?.runtime.file || entry.file,sources.runtime || sources.game],
  ]) {
    const asset = path.join(assetDir,file);
    const hash = crypto.createHash('sha256');
    for await (const bytes of fs.createReadStream(asset)) hash.update(bytes);
    if (hash.digest('hex') !== expected) throw Error('Source hash mismatch: ' + file);
    await (await page.$(selector)).uploadFile(asset);
  }
  if (entry.ngage) {
    const packageHash = crypto.createHash('sha256');
    for await (const bytes of fs.createReadStream(path.join(assetDir,entry.ngage.file))) packageHash.update(bytes);
    if (packageHash.digest('hex') !== sources.game) throw Error('Game package hash mismatch');
    await page.$eval('#app-name', (element,uid) => element.value=uid, entry.uid);
    await page.evaluate(id => {
      window.captureLaunch = startEmulator('/preload/games/' + id + '/ngage');
    }, game);
    await page.waitForFunction(() => window._gameRunning);
    const deadline = Date.now() + 180000;
    while (!complete && !errors.length && Date.now() < deadline)
      await new Promise(resolve => setTimeout(resolve, 250));
    if (!complete || errors.length) throw Error('Clean installation not completed: ' + JSON.stringify(errors));
    // Importer exit precedes the client's catalogue update. Dismiss the completed
    // installation dialogue and let its remaining guest callbacks finish.
    await new Promise(resolve => setTimeout(resolve, 5000));
    await page.screenshot({path:path.join(output,'importer-complete.png')});
    await page.keyboard.press('F2', {delay:150});
    await new Promise(resolve => setTimeout(resolve, 2000));
  } else {
    // A plain SIS installs synchronously. An empty app name prevents guest game
    // startup, so capture cannot include a player's settings or progress.
    await page.$eval('#app-name', element => element.value='');
    if (!await page.evaluate(() => startEmulator())) throw Error('SIS installation failed');
    if (await page.evaluate(() => !!window._gameRunning)) throw Error('Game ran during clean capture');
  }
  if (errors.length) throw Error('Client registration failed: ' + JSON.stringify(errors));
  await page.screenshot({path:path.join(output,'installation-complete.png')});
  // Close guest file handles and save registries before reading the files.
  await page.evaluate(() => Module.ccall('eka2l1_shutdown', null, [], []));
  const entries = await page.evaluate(() => {
    const result = [];
    function visit(root) {
      for (const name of FS.readdir(root).filter(name => name !== '.' && name !== '..').sort()) {
        const path = root + '/' + name, stat = FS.stat(path);
        const modified = Module.ccall('eka2l1_disk_modified_ms','number',['string'],[path]);
        if (modified < 0) throw Error('Could not read file metadata: ' + path);
        const metadata = {mode:stat.mode,modified};
        if ((stat.mode & 0xf000) === 0x4000) {
          result.push({path,type:'directory',size:0,...metadata}); visit(path);
        } else if ((stat.mode & 0xf000) === 0x8000) {
          result.push({path,type:'file',size:stat.size,...metadata});
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
    const chunk = new Uint8Array(256 * 1024);
    for (const entry of entries) {
      if (entry.type !== 'file') continue;
      if (FS.stat(entry.path).size !== entry.size) throw Error('File changed during capture');
      const input = FS.open(entry.path,'r');
      try {
        if (!entry.size) await window.saveInstalledChunk(entry.path,'');
        for (let start=0; start<entry.size;) {
          const count = FS.read(input,chunk,0,Math.min(chunk.length,entry.size-start),start);
          if (count <= 0) throw Error('Short capture read');
          let binary='';
          for (let i=0; i<count; i+=32768)
            binary+=String.fromCharCode(...chunk.subarray(i,Math.min(count,i+32768)));
          await window.saveInstalledChunk(entry.path,btoa(binary));
          start += count;
        }
      } finally { FS.close(input); }
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
    emulatorWasmSha256:await page.evaluate(() => {
      const url=window.ekaAssetUrls?.['/eka2l1.wasm'];
      return url ? new URL(url,location.href).searchParams.get('v') : null;
    }),
    capture:entry.ngage
      ? 'Fresh installation; importer completed; completed dialogue dismissed; client catalogue settled; shutdown closed file handles; game not launched'
      : 'Fresh SIS installation; shutdown closed file handles; game not launched'};
  fs.writeFileSync(path.join(output,game+'.json'),JSON.stringify(metadata,null,2));
  fs.writeFileSync(path.join(output,'entries.json'),JSON.stringify(entries,null,2));
  console.log(JSON.stringify(metadata));
} finally {
  await browser.close();
}
