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
  return {files,writes:0,maxWrite:0,mkdirTree() {},
    open(name:string) {++this.writes;files.set(name,Buffer.alloc(0));return name;},
    close() {}, unlink(name:string) {files.delete(name);},
    symlink(target:string,name:string) {files.set(name,files.get(target)!);},
    write(name:string,bytes:Uint8Array,start:number,count:number,offset:number) {
      this.maxWrite=Math.max(this.maxWrite,count);
      const result=Buffer.alloc(Math.max(files.get(name)!.length,offset+count));
      files.get(name)!.copy(result);
      result.set(bytes.subarray(start,start+count),offset);
      files.set(name,result);return count;
    }};
}
const restored = filesystem();
assert.deepEqual(await restore(new Response(image()),expected,restored),{files:3,bytes:7,offloadedBytes:0});
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
  {...original,entries:[{path:'/data/bad',type:'file',size:0,modified:-1}]},
  {...original,entries:[{path:'/data/bad',type:'file',size:0,modified:1e300}]},
  {...original,entries:[{path:'/data/bad',type:'file',size:0,mode:'0777'}]},
]) {
  const target=filesystem();
  await assert.rejects(restore(new Response(image(invalid)),expected,target));
  assert.equal(target.writes,0);
}
for (const contents of [Buffer.from([1,2]),Buffer.from([1,2,3,4,5,6,7,8])])
  await assert.rejects(restore(new Response(image(original,contents)),expected,filesystem()),/Truncated|Trailing/);
await assert.rejects(restore(new Response('error',{status:404}),expected,filesystem()),/HTTP 404/);
// A file larger than the streaming buffer must not produce whole-file writes.
// Real WasmFS/OPFS persistence and symlinks are covered by the browser route.
const large=crypto.randomBytes(1024*1024+17);
const largeHeader={...original,entries:[{path:'/data/large',type:'file',size:large.length}]};
const streamed=filesystem();
const destination=()=>'/opfs/large';
assert.deepEqual(await restore(new Response(image(largeHeader,large)),expected,streamed,{destination}),
  {files:1,bytes:large.length,offloadedBytes:large.length});
assert.equal(streamed.maxWrite,256*1024);
assert.deepEqual(streamed.files.get('/data/large'),large);
for (const failure of ['truncated','trailing','checksum','write','link','metadata']) {
  const target=filesystem();
  let archive=image(largeHeader,large);
  if(failure==='truncated') archive=image(largeHeader,large.subarray(0,large.length-1));
  if(failure==='trailing') archive=image(largeHeader,Buffer.concat([large,Buffer.from([1])]));
  if(failure==='checksum') archive[archive.length-8]^=1;
  if(failure==='write') target.write=()=>0;
  if(failure==='link') target.symlink=()=>{throw Error('link failed');};
  const metadata=()=>{if(failure==='metadata')throw Error('metadata failed');};
  await assert.rejects(restore(new Response(archive),expected,target,{destination,metadata}),undefined,failure);
  assert.equal(target.files.size,0,failure+' must discard partial files');
}
await import('../../emu/wasm/persistent-disk.js');
const disk=Object.create((globalThis as any).EkaDeviceDisk.prototype);
Object.assign(disk,{manifest:null,romOffloadRoot:'/opfs/rom',firmwareOffloadRoot:'/opfs/firmware'});
for(const [file,size,target] of [
  ['/data/roms/rm-409/SYM.ROM',4*1024*1024,'/opfs/rom/rm-409/SYM.ROM'],
  ['/data/drives/z/rm-409/large',64*1024,'/opfs/firmware/rm-409/large'],
  ['/data/drives/z/rm-409/small',64*1024-1,'/data/drives/z/rm-409/small'],
  ['/data/drives/c/game',8*1024*1024,'/data/drives/c/game'],
]) assert.equal(disk.installedImageTarget({path:file,size}),target);
disk.romOffloadRoot=null;
assert.equal(disk.installedImageTarget({path:'/data/roms/rm-409/SYM.ROM',size:8*1024*1024}),'/data/roms/rm-409/SYM.ROM');
disk.manifest={};
assert.throws(()=>disk.installedImageTarget({path:'/data/save',size:1}),/saved device/);
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
console.log('PASS installed image identity, bounded writes, offload selection, rollback, saved-device guard and asset integrity');
