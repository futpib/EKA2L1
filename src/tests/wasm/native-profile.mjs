// Linux native samples and read-only V8 metadata snapshots. This adapter is
// deliberately version-locked: an unrecognized layout must never invent PCs.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';

export const supportedV8 = '15.3.76.13';
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
export function readJitdump(file, includeCode = false) {
  const b = fs.readFileSync(file), rows = [];
  assert(b.length >= 40 && b.readUInt32LE(0) === 0x4a695444 && b.readUInt32LE(4) === 1);
  assert(b.readUInt32LE(12) === 62 && b.readBigUInt64LE(32) === 0n, 'Expected x64 monotonic jitdump');
  let pos = b.readUInt32LE(8);
  for (; pos + 16 <= b.length;) {
    const kind = b.readUInt32LE(pos), size = b.readUInt32LE(pos+4);
    assert(size >= 16, 'Invalid jitdump record');
    if (pos + size > b.length) break; // The live logger can have an unflushed tail.
    if (kind === 0) {
      const end = b.indexOf(0, pos+56); assert(end >= pos+56 && end < pos+size);
      const length = Number(b.readBigUInt64LE(pos+40));
      assert(end + 1 + length === pos+size);
      const code = b.subarray(end+1, pos+size);
      rows.push({pid:b.readUInt32LE(pos+16), tid:b.readUInt32LE(pos+20),
        timestamp:Number(b.readBigUInt64LE(pos+8)), address:Number(b.readBigUInt64LE(pos+32)),
        size:length, id:Number(b.readBigUInt64LE(pos+48)), name:b.toString('utf8',pos+56,end),
        sha256:hash(code), ...(includeCode ? {code} : {})});
    } else if (kind === 1) throw Error('JIT code movement requires a new lifetime adapter');
    pos += size;
  }
  return {rows, trailing_bytes:b.length-pos};
}

export function decodePositions(bytes) {
  // PositionTableEntry starts at kFunctionEntryBytecodeOffset (-1), including
  // for native WASM tables. Starting at zero shifts every anchor by one byte.
  const rows=[]; let offset=0, pc=-1, source=0n;
  function leb() {
    let v=0n, shift=0n;
    for(let n=0;n<10 && offset<bytes.length;n++) {
      const c=bytes[offset++];v|=BigInt(c&127)<<shift;
      if(!(c&128))return v;shift+=7n;
    }
    throw Error('Invalid V8 source-position LEB');
  }
  while(offset<bytes.length) {
    const entry=leb(), raw=leb(), delta=(raw>>1n)^(-(raw&1n));
    pc+=Number(entry>>3n);source=entry&4n ? delta : source+delta;
    assert(source>=0n && source<(1n<<47n));
    rows.push({native_offset:pc, wasm_offset:Number((source>>1n)&0x3fffffffn)-1,
      inlining_id:Number((source>>31n)&0xffffn)-1, external:!!(source&1n)});
  }
  return rows;
}

export async function startNativeProfile(output, pids, version) {
  assert.equal(version.jsVersion, supportedV8, 'Update and validate the V8 metadata adapter first');
  const file=path.join(output,'native-samples.json');
  const child=spawn('python3',[fileURLToPath(new URL('../benchmark/sample_native.py',import.meta.url)),file,...pids.map(String)],{stdio:['pipe','pipe','pipe']});
  let errors='', messages='';child.stderr.on('data',c=>errors+=c);
  const completion=new Promise((resolve,reject)=>{
    child.on('error',reject);child.on('exit',code=>code===0?resolve():reject(Error(`Native sampler ${code}: ${errors}`)));
  });
  // Install the rejection handler immediately, including errors during startup.
  completion.catch(()=>{});
  await Promise.race([completion.then(()=>{throw Error('Sampler exited before READY')}),
    new Promise(resolve=>child.stdout.on('data',c=>{messages+=c;if(messages.includes('READY\n'))resolve()}))]);
  return {async stop(){child.stdin.end('\n');await completion;}, async abort(){child.stdin.end();await completion.catch(()=>{});}};
}

export function snapshotNativeMetadata(output, version, maxFunctions=160) {
  assert.equal(version.jsVersion,supportedV8);
  const sample=JSON.parse(fs.readFileSync(path.join(output,'native-samples.json')));
  const logs=fs.readdirSync(output).filter(n=>/^jit-\d+\.dump$/.test(n));
  const loads=logs.flatMap(n=>readJitdump(path.join(output,n)).rows), hits=new Map();
  // Build pages for address lookup, preserving all versions and load timestamps.
  const pages=new Map();
  for(const code of loads)for(let p=Math.floor(code.address/4096);p<=Math.floor((code.address+code.size-1)/4096);p++) {
    const key=`${code.pid}:${p}`;if(!pages.has(key))pages.set(key,[]);pages.get(key).push(code);
  }
  for(const [time,pid,,ip] of sample.samples) {
    const candidates=(pages.get(`${pid}:${Math.floor(ip/4096)}`)||[])
      .filter(c=>c.address<=ip && ip<c.address+c.size && c.timestamp<=time).sort((a,b)=>b.timestamp-a.timestamp);
    const c=candidates[0];if(c && /-\d+-(turbofan|liftoff)$/.test(c.name))hits.set(c,(hits.get(c)||0)+1);
  }
  const selected=[...hits].sort((a,b)=>b[1]-a[1]).slice(0,maxFunctions).map(([c,n])=>({...c,samples:n}));
  const result={version,adapter_version:1,layout:'V8 15.3.76.13 WasmCode x64, 112 bytes',
    scope:'Read after sampling, before debugger attachment. Sparse call/trap positions; not all instructions.',
    selected,found:[],errors:[]};
  for(const pid of new Set(selected.map(c=>c.pid))) {
    const wanted=selected.filter(c=>c.pid===pid), low=new Map();
    for(const c of wanted){const k=c.address>>>0;if(!low.has(k))low.set(k,[]);low.get(k).push(c)}
    const fd=fs.openSync(`/proc/${pid}/mem`,'r');
    const read=(address,size)=>{const b=Buffer.alloc(size);assert.equal(fs.readSync(fd,b,0,size,address),size);return b};
    try {
      const maps=fs.readFileSync(`/proc/${pid}/maps`,'utf8');fs.writeFileSync(path.join(output,`native-${pid}.maps`),maps);
      for(const line of maps.split('\n')) {
        const m=/^([0-9a-f]+)-([0-9a-f]+) (rw-p) \S+ \S+ \S+\s*(.*)$/.exec(line);
        if(!m||m[4].startsWith('/'))continue;
        const begin=parseInt(m[1],16),end=parseInt(m[2],16);
        for(let start=begin;start<end;start+=4*1024*1024) {
          let b;try{b=read(start,Math.min(4*1024*1024+112,end-start))}catch{continue}
          for(let pos=0;pos+8<=b.length;pos+=8) {
            const possible=low.get(b.readUInt32LE(pos));if(!possible)continue;
            const address=Number(b.readBigUInt64LE(pos));
            for(const code of possible.filter(c=>c.address===address)) {
              const objectAddress=start+pos-8;
              try {
                const object=read(objectAddress,112), index=object.readInt32LE(56), flags=object[104];
                const match=/-(\d+)-(turbofan|liftoff)$/.exec(code.name);
                if(object.readUInt32LE(32)!==code.size || index!==Number(match[1]) || (flags&7)!==0 || ((flags>>3)&3)!==(match[2]==='turbofan'?2:1))continue;
                const sizes=[36,40,44,48,52].map(n=>object.readUInt32LE(n));
                if(sizes.some(n=>n>4*1024*1024))continue;
                const [reloc,source,inline,,trap]=sizes, meta=Number(object.readBigUInt64LE(24));
                if(trap%4 || inline%13)continue;
                const current=read(address,code.size);if(hash(current)!==code.sha256)continue;
                const sourceBytes=read(meta+trap+reloc,source), positions=decodePositions(sourceBytes);
                if(positions.some(p=>p.native_offset < 0 || p.native_offset>=code.size || p.external))continue;
                const inlining=read(meta+trap+reloc+source,inline), inlinees=[];
                for(let i=0;i<inline;i+=13) {
                  const raw=inlining.readBigUInt64LE(i+5);
                  inlinees.push({function_index:inlining.readInt32LE(i),tail:!!inlining[i+4],
                    caller_offset:Number((raw>>1n)&0x3fffffffn)-1,caller_inlining_id:Number((raw>>31n)&0xffffn)-1});
                }
                const trapping=read(meta,trap), traps=[];
                for(let i=0;i<trap;i+=4)traps.push(trapping.readUInt32LE(i));
                if(traps.some(n=>n>=code.size))continue;
                if(!object.subarray(0,106).equals(read(objectAddress,106)))continue;
                if(result.found.some(c=>c.pid===pid && c.id===code.id))continue;
                fs.writeFileSync(path.join(output,`native-${pid}-${code.id}.bin`),current);
                result.found.push({...code,object_address:objectAddress,native_module:Number(object.readBigUInt64LE(0)),
                  function_index:index,source_positions_hex:sourceBytes.toString('hex'),positions,inlinees,traps});
              }catch(error){result.errors.push({pid,id:code.id,error:String(error)})}
            }
          }
        }
      }
    }finally{fs.closeSync(fd)}
  }
  fs.writeFileSync(path.join(output,'native-metadata.json'),JSON.stringify(result));
  return {selected:selected.length,found:result.found.length,positions:result.found.reduce((n,c)=>n+c.positions.length,0)};
}
