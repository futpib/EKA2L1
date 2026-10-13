import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {loadInstalledImage} from './installed-image.ts';
await import('../../emu/wasm/preinstalled-image.js');
const restore = (globalThis as any).EkaInstalledImage.restore;
const expected = {game:'hooked-on', sources:{rom:'rom',game:'game'}};
const entries = [
  {path:'/data/drives',type:'directory',size:0},
  {path:'/data/drives/c',type:'directory',size:0},
  {path:'/data/devices.yml',type:'file',size:4},
  {path:'/data/drives/c/save',type:'file',size:0},
  {path:'/data/drives/c/game',type:'file',size:3},
];
const original = {version:1,...expected,entries};
function image(header: any = original, contents = Buffer.from([1,2,3,4,5,6,7])) {
  const json=Buffer.from(JSON.stringify(header)),prefix=Buffer.alloc(4);
  prefix.writeUInt32LE(json.length);
  return gzipSync(Buffer.concat([prefix,json,contents]));
}
function filesystem() {
  const files = new Map<string,Buffer>();
  return {files,writes:0,mkdirTree() {},
    open(name:string) {++this.writes;files.set(name,Buffer.alloc(0));return name;},
    close() {}, writeFile(name:string,bytes:Uint8Array) {
      // Match the SDK's append behavior. Explicit truncation is required.
      files.set(name,Buffer.concat([files.get(name)!,bytes]));
    }};
}
const restored = filesystem();
assert.deepEqual(await restore(new Response(image()),expected,restored),{files:3,bytes:7});
assert.deepEqual(restored.files.get('/data/devices.yml'),Buffer.from([1,2,3,4]));
assert.deepEqual(restored.files.get('/data/drives/c/game'),Buffer.from([5,6,7]));
assert.equal(restored.files.get('/data/drives/c/save')!.length,0);
restored.files.set('/data/drives/c/game',Buffer.from('modified'));
await restore(new Response(image()),expected,restored);
assert.deepEqual(restored.files.get('/data/drives/c/game'),Buffer.from([5,6,7]));
// Source property order is not part of the image identity.
await restore(new Response(image({...original,sources:{game:'game',rom:'rom'}})),expected,filesystem());
for (const invalid of [
  {...original,version:2}, {...original,game:'tetris'}, {...original,sources:{rom:'different',game:'game'}},
  {...original,entries:[...entries,{path:'/data/../tmp/x',type:'file',size:0}]},
  {...original,entries:[...entries,{path:'/data/drives/c/game',type:'file',size:0}]},
  {...original,entries:entries.filter(entry=>entry.path!=='/data/drives/c')},
  {...original,entries:[{path:'/data/oversized',type:'file',size:1024*1024*1024+1}]},
]) {
  const target=filesystem();
  await assert.rejects(restore(new Response(image(invalid)),expected,target));
  assert.equal(target.writes,0);
}
for (const contents of [Buffer.from([1,2]),Buffer.from([1,2,3,4,5,6,7,8])])
  await assert.rejects(restore(new Response(image(original,contents)),expected,filesystem()),/Truncated|Trailing/);
await assert.rejects(restore(new Response('error',{status:404}),expected,filesystem()),/HTTP 404/);
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'eka-image-test-'));
try {
  assert.equal(await loadInstalledImage(directory,expected.game,expected.sources),undefined);
  const archive=image(),file=expected.game+'.img.gz';
  fs.writeFileSync(path.join(directory,file),archive);
  const metadata={version:1,...expected,file,sha256:crypto.createHash('sha256').update(archive).digest('hex')};
  const manifest=path.join(directory,expected.game+'.json');
  fs.writeFileSync(manifest,JSON.stringify(metadata));
  assert.equal((await loadInstalledImage(directory,expected.game,expected.sources))!.image.game,expected.game);
  await assert.rejects(loadInstalledImage(directory,expected.game,{rom:'changed',game:'game'}),/source\/version mismatch/);
  fs.appendFileSync(path.join(directory,file),'changed');
  await assert.rejects(loadInstalledImage(directory,expected.game,expected.sources),/SHA-256 mismatch/);
  fs.writeFileSync(manifest,JSON.stringify({...metadata,file:'../escape'}));
  await assert.rejects(loadInstalledImage(directory,expected.game,expected.sources),/source\/version mismatch/);
} finally {fs.rmSync(directory,{recursive:true,force:true});}
console.log('PASS installed image identity, exact bytes, fresh writes, invalid paths, truncation and asset integrity');
