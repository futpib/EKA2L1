import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {decodePositions,readJitdump} from './native-profile.mjs';

test('decode V8 call/trap source anchors, not interpolated instruction lines',()=>{
 assert.deepEqual(decodePositions(Buffer.from('8a0460ca0637','hex')),[
  {native_offset:64,wasm_offset:23,inlining_id:-1,external:false},
  {native_offset:169,wasm_offset:9,inlining_id:-1,external:false}]);
 assert.throws(()=>decodePositions(Buffer.from([0x80])));
 assert.deepEqual(decodePositions(Buffer.alloc(0)),[]);
});
test('retain load time, code identity, address and bytes, reject movement',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'eka-jit-'));
 try {
  const header=Buffer.alloc(40);header.writeUInt32LE(0x4a695444);header.writeUInt32LE(1,4);header.writeUInt32LE(40,8);header.writeUInt32LE(62,12);
  const name=Buffer.from('JS:f_123-7-turbofan\0');const code=Buffer.from([0xc3]);const record=Buffer.alloc(56+name.length+code.length);
  record.writeUInt32LE(record.length,4);record.writeBigUInt64LE(100n,8);record.writeUInt32LE(20,16);record.writeUInt32LE(21,20);
  record.writeBigUInt64LE(0x1000n,32);record.writeBigUInt64LE(1n,40);record.writeBigUInt64LE(88n,48);name.copy(record,56);code.copy(record,56+name.length);
  const file=path.join(dir,'test.dump');fs.writeFileSync(file,Buffer.concat([header,record,record.subarray(0,20)]));
  const parsed=readJitdump(file,true);assert.equal(parsed.trailing_bytes,20);assert.equal(parsed.rows.length,1);
  const row=parsed.rows[0];assert.equal(row.address,0x1000);assert.equal(row.timestamp,100);assert.equal(row.id,88);assert.deepEqual(row.code,code);
  record.writeUInt32LE(1,0);fs.writeFileSync(file,Buffer.concat([header,record]));assert.throws(()=>readJitdump(file),/movement/);
 }finally{fs.rmSync(dir,{recursive:true,force:true})}
});
