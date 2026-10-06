// Offline counts and exact state comparison for a captured ARM idivmod helper.
// Never used in game timing or normal guest execution.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {counted,resetCost,readCost} from './wasm-step-counter.mjs';
const [input,output,name]=process.argv.slice(2);
if(!name)throw Error('division-counts.mjs MODULE_COMPARISON.json OUTPUT.json DIVISION_EXPORT');
const comparison=JSON.parse(fs.readFileSync(input));
const row=comparison.changed.find(r=>r.name===name);
assert(row && row.thumb===false,'Expected a changed ARM integer-division helper');
const memory=new WebAssembly.Memory({initial:256,maximum:65536,shared:true});
const state=new Uint32Array(memory.buffer,65536,256);
const env={memory};for(const name of ['tlb_read32','tlb_write32','tlb_read8','tlb_write8','tlb_read16','tlb_write16'])env[name]=()=>{throw Error('unexpected memory callback');};
const modules=['before','after'].map(v=>new WebAssembly.Instance(counted(fs.readFileSync(comparison[v].directory+'/'+row[v].module),{breakdown:true}),{env}));
let random=0x817abca3;const next=()=>{random^=random<<13;random^=random>>>17;random^=random<<5;return random>>>0;};
let checks=0,reduced=0,unchanged=0;const examples=[];
for(let sample=0;sample<128;++sample) {
 const numerator=sample<16?0x40000000:next();const divisor=sample<16?[0x101e,1,2,3,7,255,256,257,65535,65536,0x7fffffff,0x80000000,0xffffffff,0xffffefe2,0x100000,0x200000][sample]:next()||1;
 const initial=Array.from({length:256},next);
 initial[0]=numerator;initial[1]=divisor;initial[14]=0x12345;initial[15]=Number(row.guest_pc);
 initial[784/4]=16;initial[796/4]=16;initial[828/4]=0;initial[864/4]=0;initial[876/4]=1;
 for(const offset of [804,808,812,816])initial[offset/4]&=1;
 for(const budget of [0,1,2,3,4,7,8,9,11,12,13,17,18,23,31,32,33,47,63,64,65,87,88,89,116,256]) {
  const results=modules.map(instance=>{state.set(initial);state[848/4]=budget;resetCost(instance);const result=instance.exports[row.name](65536);return {result,state:[...state],cost:readCost(instance)};});
  const [a,b]=results;assert.equal(a.result,b.result);assert.deepEqual(a.state,b.state);assert(b.cost.operations<=a.cost.operations,`${sample}/${budget}: ${a.cost.operations} -> ${b.cost.operations}`);
  ++checks;if(b.cost.operations<a.cost.operations)++reduced;else ++unchanged;
  if(sample===0)examples.push({numerator,divisor,budget,count:a.result,before:a.cost,after:b.cost});
 }
}
const result={name,input,checks,reduced,unchanged,examples};fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');console.log({checks,reduced,unchanged,full:examples.find(e=>e.budget===256)});
