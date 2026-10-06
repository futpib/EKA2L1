import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {nativeInventory,nativeCodeRange,profileCost} from './v8-native-cost.mjs';

// Address generation and alignment padding are not actual memory operations;
// stack operands are reported separately without labelling every one a spill.
assert.deepEqual(nativeInventory(`
  0: 48 8d 04 01        lea rax,[rcx+rax*1]
  4: 8b 45 08           mov eax,DWORD PTR [rbp+0x8]
  7: 89 00              mov DWORD PTR [rax],eax
  9: 74 03              je 0xe
  b: ff d0              call rax
  d: c3                 ret
  e: 0f 1f 00           nop DWORD PTR [rax]
 11: cc                 int3
`),{instructions:7,padding_instructions:1,memory_instructions:2,stack_memory_instructions:1,
    conditional_branches:1,calls:1});
assert.throws(()=>nativeInventory(' 0: ff (bad)'),/Undecoded/);
assert.throws(()=>nativeInventory(''),/Empty/);

const code=Buffer.alloc(24);code.writeBigUInt64LE(0x100cn,16);
const assembly=`
 0: 4c 8d 15 09 00 00 00 lea r10,[rip+0x9] # 0x10
 7: 41 ff 24 da jmp QWORD PTR [r10+rbx*8]
 c: c3 ret
10: 0c 10 or al,0x10
`;
assert.deepEqual(nativeCodeRange(code,0x1000,assembly),{executable_bytes:16,table_bytes:8,table_offsets:[16]});
code.writeBigUInt64LE(0x100dn,16);
assert.throws(()=>nativeCodeRange(code,0x1000,assembly),/jump-table entry/);

const dir=fs.mkdtempSync(path.join(os.tmpdir(),'eka-profile-cost-'));
try {
    fs.writeFileSync(path.join(dir,'capture-worker.json'),JSON.stringify({selected:'worker-7'}));
    const node=(id,name,children=[])=>({id,callFrame:{functionName:name},children});
    fs.writeFileSync(path.join(dir,'worker-7.cpuprofile'),JSON.stringify({nodes:[
        node(1,'root',[2,5,6,7,8,9]),node(2,'translate_arm',[3]),node(3,'state_local_cache::finish',[4]),
        node(4,'malloc'),node(5,'f_4096'),node(6,'r_7_pc_4096'),node(7,'f_8192_budget_short'),
        node(8,'f_8192_memory_fallback'),node(9,'not_guest_f_8192')],
        samples:[4,3,2,5,6,7,8,9],timeDeltas:[10,20,30,40,50,60,70,80]}));
    const p=profileCost(dir);
    assert.equal(p.sampled_us,360);assert.equal(p.guest_self_us,220);
    assert.equal(p.rom_self_us,40);assert.equal(p.ram_self_us,50);assert.equal(p.private_self_us,130);
    assert.deepEqual(p.inclusive_us,{translation:60,state_finalization:30,allocation:10});
} finally {fs.rmSync(dir,{recursive:true,force:true});}
console.log('PASS native inventory and overlapping compiler profile attribution');
