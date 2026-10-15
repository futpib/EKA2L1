import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {loadRomCache} from './rom-cache.ts';
await import('../../emu/wasm/rom-cache.js');
const globals = globalThis as any;
const prepare = globals.EkaRomCache.prepare;
const build = 'a'.repeat(64), data = Buffer.from('derived ROM cache');
const digest = crypto.createHash('sha256').update(data).digest('hex');
const expected = {url:'/preload/rom-cache/asphalt-3',buildSha256:build,sha256:digest,bytes:data.length};
const files = new Map<string,Uint8Array>();
let configured = 0;
globals.location = {href:'https://example.test/'};
globals.ekaAssetUrls = {'/eka2l1.wasm':'/eka2l1.wasm?v=' + build};
globals.ekaDownload = async () => new Response(data);
globals.FS = {open(name:string) {files.set(name,new Uint8Array());return name;},close() {},
  writeFile(name:string,bytes:Uint8Array) {files.set(name,new Uint8Array(bytes));},unlink(name:string) {files.delete(name);}};
globals.Module = {_eka2l1_rom_cache_configure() {},ccall(name:string,result:string,types:string[],args:string[]) {
  assert.equal(name,'eka2l1_rom_cache_configure');assert.deepEqual(args,['/tmp/rom-cache.bin',build]);++configured;return 0;
}};
await prepare(expected);
assert.equal(globals.ekaRomCacheStatus.status,'staged');assert.equal(configured,1);
assert.deepEqual(files.get('/tmp/rom-cache.bin'),new Uint8Array(data));
for (const bad of [{...expected,buildSha256:'b'.repeat(64)},{...expected,sha256:'b'.repeat(64)},{...expected,bytes:1}]) {
  await prepare(bad);assert.equal(globals.ekaRomCacheStatus.status,'fallback');assert.equal(configured,1);assert.equal(files.size,0);
}
globals.ekaDownload = async () => new Response('missing',{status:404});
await prepare(expected);assert.equal(globals.ekaRomCacheStatus.status,'fallback');assert.equal(configured,1);
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'eka-rom-cache-test-'));
try {
  assert.equal(await loadRomCache(directory,'asphalt-3',build,'rom',2,3),null);
  const folder=path.join(directory,'asphalt-3');fs.mkdirSync(folder);
  const manifest={version:1,game:'asphalt-3',buildSha256:build,romSha256:'rom',memoryImpl:2,unsafeCode:3,sha256:digest,bytes:data.length};
  fs.writeFileSync(path.join(folder,'manifest.json'),JSON.stringify(manifest));
  fs.writeFileSync(path.join(folder,'rom-cache.bin'),data);
  assert.deepEqual((await loadRomCache(directory,'asphalt-3',build,'rom',2,3))!.cache,expected);
  for (const [binary,rom,memory,unsafe] of [['other','rom',2,3],[build,'other',2,3],[build,'rom',0,3],[build,'rom',2,0]] as const)
    assert.equal(await loadRomCache(directory,'asphalt-3',binary,rom,memory,unsafe),null);
  fs.writeFileSync(path.join(folder,'rom-cache.bin'),'wrong');
  await assert.rejects(loadRomCache(directory,'asphalt-3',build,'rom',2,3),/content mismatch/);
} finally {fs.rmSync(directory,{recursive:true,force:true});}
console.log('ROM cache: browser integrity/fallback and server build/ROM/policy checks passed');
