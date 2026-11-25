import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
import path from 'node:path';
import {parseArgs} from 'node:util';
const {values}=parseArgs({options:Object.fromEntries(['build','helper','output'].map(n=>[n,{type:'string'}]))});
for(const name of ['build','helper','output'])assert(values[name],'Missing --'+name);
const build=path.resolve(values.build),helper=path.resolve(values.helper),output=path.resolve(values.output);
assert(!fs.existsSync(output));fs.mkdirSync(output,{recursive:true});
const create=(await import(pathToFileURL(build+'/fixture.mjs'))).default;
const {createAudioMixFactory}=await import(pathToFileURL(helper));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const receipt=JSON.parse(fs.readFileSync(build+'/build.json'));assert(receipt.pass);
assert.equal(sha(fs.readFileSync(build+'/fixture.mjs')),receipt.outputs['fixture.mjs']);
const row={"asid": 1, "pc": 1879049864, "words": [3818974335, 3818918146, 3780902916, 3801014527, 3785365569, 2315255855], "sources": [{"pc": 1879049864, "words": [3818974335, 3818918146, 3780902916, 3801014527, 3785365569, 2315255855, 3767150636, 3788648656, 3788841200, 3760333721, 3780247566, 3248496654, 3388997633, 3780247553, 2980061185, 3851432016, 3800457217, 3767283715, 3780509704, 3771015346, 3785408515, 3850383436, 989855720, 3904735216]}]},base=0x100000;
async function setup(selected){
 const m=await create({print(){},printErr(){}});m._ref_create();m._ref_trace(0);m.code=m._ref_map(0x70000000,0x10000,5);m.data=m._ref_map(base,0x8000,3);
 new Uint32Array(m.HEAPU8.buffer).set(row.sources[0].words,(m.code+0x688)>>>2);new Uint32Array(m.HEAPU8.buffer)[(m.code+0x7000)>>>2]=0xeafffffe;
 if(selected){const dummy={asid:1,pc:0x70008000,words:[0xe2522001,0x1afffffd],function:1,complete:true},unused=()=>{throw Error('Fallback');};
  m.countedBlockFactory=()=>({entries:[{...row,function:0,complete:true},dummy],functions:[unused,unused]});
  m.loopBlockFactory=()=>({abi:'counted-original-arm-loop-v2',entries:[{...dummy,function:0}],functions:[unused]});m.semanticFactory=createAudioMixFactory([row]);
  assert.equal(m._eka2l1_counted_configure(1),2);assert.equal(m._eka2l1_loop_configure(1),1,m.__semanticError);assert.equal(m._eka2l1_loop_packed_pixel_configure(1),1);
 }return m;
}
const initial=Uint8Array.from({length:0x8000},(_,i)=>(i*17+21)&255);
function seed(m,{sample=127,volume=256,old=200,step=65536,flags=0,fraction=0,bound=0x10000000,index=0,limit=100,dest=base+0x3000,source=base+0x1000}={}){
 const b=m.HEAPU8,w=new Uint32Array(b.buffer),h=new Uint16Array(b.buffer),at=a=>m.data+a-base;
 b.set(initial,m.data);b.fill(sample,at(base+0x1000),at(base+0x2000));h.fill(old,at(base+0x3000)>>>1,at(base+0x4000)>>>1);w[at(base+0x150)>>>2]=step;
 w[at(base+0x7020)>>>2]=0x70007000;
 m._ref_cpsr((0x10|flags)>>>0);for(let i=0;i<16;i++)m._ref_register(i,0x12340000+i);
 for(const[r,v]of[[0,base+0x100],[4,bound],[5,dest],[6,index],[8,limit],[9,volume],[10,source],[12,fraction],[13,base+0x7000],[15,row.pc]])m._ref_register(r,v);
 m._ref_irq(0);m._ref_enable_tlb(1);m._ref_generation();m._ref_clear_code();
}
const fields=['Reg','Reg_usr','Reg_svc','Reg_abort','Reg_undef','Reg_irq','Reg_firq','Spsr','CP15','VFP','ExtReg','Cpsr','Spsr_copy','Mode','Bank','NFlag','ZFlag','CFlag','VFlag','IFFlags','shifter_carry_out','TFlag','NresetSig','NfiqSig','NirqSig','abortSig','NtransSig','bigendSig','syscallSig','fault','fault_address'];
const state=m=>{const s=JSON.parse(m.UTF8ToString(m._ref_snapshot()));return Object.fromEntries(fields.map(k=>[k,s[k]]));};
const a=await setup(false),b=await setup(true),report={pass:false,scope:'Exact original ARM CPU and normal RAM differential prefixes, bounded authenticated audio backedges; no whole-game performance claim.',cases:0,errors:[],controls:[],inputs:Object.fromEntries([new URL(import.meta.url),helper,build+'/build.json',build+'/fixture.mjs'].map(p=>[String(p),sha(fs.readFileSync(p))]))};
try{
 for(const sample of [-128,-1,0,1,127])for(const volume of [-2147483648,-32768,-1,1,256,2147483647])for(const old of [-32768,0,32767])for(const flags of [0,0xf0000000])for(const budget of [1,20,21,22,23,24,42,45,46,47,100,1000]){
  const config={sample,volume,old,flags};seed(a,config);seed(b,config);const expected=a._ref_run(budget);assert.equal(b._ref_semantic_run(budget),expected,JSON.stringify({config,budget}));
  const x=state(a),y=state(b);for(const k of fields)assert.deepEqual(y[k],x[k],JSON.stringify({config,budget,field:k}));
  assert.deepEqual(Buffer.from(b.HEAPU8.buffer,b.data,initial.length),Buffer.from(a.HEAPU8.buffer,a.data,initial.length),'RAM');report.cases++;
 }

 for(const config of [{step:0},{step:1},{step:65535},{step:0xffffffff},{fraction:0xffff,bound:0x10000},{fraction:0x10001,bound:0x10000},{index:98,limit:100},{index:99,limit:100},{index:0xffffffff,limit:100},{index:0x7ffffffe,limit:0x80000001},{source:base+0xfff},{dest:base+0x3ffe},{dest:base+0x1000},{dest:base+0x14c},{dest:base+0x150},{fraction:0xffffffff,source:base-65535,step:1}])for(const budget of [1,20,21,22,23,24,42,45,46,47,100,1000]){
  seed(a,config);seed(b,config);const expected=a._ref_run(budget);assert.equal(b._ref_semantic_run(budget),expected,JSON.stringify({config,budget}));
  assert.deepEqual(state(b),state(a),JSON.stringify({config,budget}));assert.deepEqual(Buffer.from(b.HEAPU8.buffer,b.data,initial.length),Buffer.from(a.HEAPU8.buffer,a.data,initial.length),'RAM');report.cases++;
 }
 const unchanged=(name,prepare=()=>{},budget=230)=>{seed(b);prepare();const before=state(b),ram=Buffer.from(b.HEAPU8.subarray(b.data,b.data+initial.length));assert.equal(b._ref_semantic_direct(budget,0),0,name);assert.deepEqual(state(b),before,name);assert.deepEqual(Buffer.from(b.HEAPU8.buffer,b.data,initial.length),ram,name);report.controls.push(name);};
 for(const budget of [0,1,20])unchanged('short-'+budget,()=>{},budget);
 unchanged('wrong-pc',()=>b._ref_register(15,row.pc+4));unchanged('wrong-asid',()=>b._ref_asid(2));b._ref_asid(1);
 unchanged('pending-irq',()=>b._ref_irq(1));b._ref_irq(0);
 for(let i=0;i<row.sources[0].words.length;i++){unchanged('source-drift-'+i,()=>{b.HEAPU8[b.code+0x688+i*4]^=1;b._ref_generation();});b.HEAPU8[b.code+0x688+i*4]^=1;b._ref_generation();b._ref_clear_code();}
 for(const [name,address,mode]of [['source',base+0x1000,1],['old-output',base+0x3000,1],['new-output',base+0x3000,2],['step',base+0x150,1],['position',base+0x14c,2]]){unchanged(name+'-denied',()=>b._ref_deny(address,address+4,mode));b._ref_deny(0,0,0);}
 unchanged('unaligned-output',()=>b._ref_register(5,base+0x3001));unchanged('unaligned-object',()=>b._ref_register(0,base+0x101));unchanged('big-endian',()=>b._ref_cpsr(0x210));unchanged('final-iteration',()=>b._ref_register(6,99));unchanged('fraction-exits',()=>b._ref_register(12,0xffffffff));
 seed(b,{step:0,limit:2000});assert.equal(b._ref_semantic_direct(100000,0),23*1024);report.controls.push('bounded-1024');

 seed(a);seed(b);const previous=b.data;b.data=b._ref_remap(base);assert.notEqual(b.data,previous);assert.equal(b._ref_semantic_run(230),a._ref_run(230));assert.deepEqual(state(b),state(a));assert.deepEqual(Buffer.from(b.HEAPU8.buffer,b.data,initial.length),Buffer.from(a.HEAPU8.buffer,a.data,initial.length));report.controls.push('remapped-data');
 seed(b,{sample:0});assert.equal(b._ref_semantic_direct(230,0),230);const resolutions=b._eka2l1_semantic_resolutions();
 const rewind=()=>{for(const[r,v]of[[5,base+0x3000],[6,0],[12,0]])b._ref_register(r,v);};
 rewind();assert.equal(b._ref_semantic_direct(230,0),230);assert.equal(b._eka2l1_semantic_resolutions(),resolutions);report.controls.push('same-generation-cache');
 rewind();b._ref_deny(base+0x3000,base+0x3004,2);const before=state(b),ram=Buffer.from(b.HEAPU8.subarray(b.data,b.data+initial.length));assert.equal(b._ref_semantic_direct(230,0),0);assert.deepEqual(state(b),before);assert.deepEqual(Buffer.from(b.HEAPU8.buffer,b.data,initial.length),ram);report.controls.push('warm-write-revocation');
 b._ref_deny(0,0,0);assert.equal(b._ref_semantic_direct(230,0),230);report.controls.push('warm-write-restoration');
 report.stats=b.__semanticBindings;assert(report.stats.calls>0);report.pass=true;
}catch(e){report.errors.push(e.stack);}finally{a._ref_destroy();b._ref_destroy();}
fs.writeFileSync(output+'/report.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report));if(!report.pass)process.exitCode=1;
