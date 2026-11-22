import fs from 'node:fs';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
import path from 'node:path';
import {parseArgs} from 'node:util';
const {values}=parseArgs({options:Object.fromEntries(['build','helper','output'].map(n=>[n,{type:'string'}]))});
for(const n of ['build','helper','output'])assert(values[n],'Missing --'+n);
const build=path.resolve(values.build),helper=path.resolve(values.helper);
const create=(await import(pathToFileURL(build+'/fixture.mjs'))).default;
const {createIndexedRowFactory}=await import(pathToFileURL(helper));
const out=values.output; assert(out&&!fs.existsSync(out));fs.mkdirSync(out,{recursive:true});
const sha=x=>createHash('sha256').update(x).digest('hex');
const pc=0x10100,codeBase=0x10000,dataBase=0x100000;
const words=[0xe5d53000,0xe3530000,0xe1a01083,0x119110b0,0x11c410b0,0xe2866001,0xe2855001,0xe2844002,0x15d8c010,0xe15c0006,0xcafffff4];
const row={asid:1,pc,words,sources:[{pc,words}]};
const report={pass:false,scope:'Original ARM interpreter versus selected JS indexed row; fixture normal RAM, exact CPU flags/registers and memory at bounded instruction prefixes. No product speed claim.',inputs:{},cases:[],controls:[],errors:[]};
const receipt=JSON.parse(fs.readFileSync(build+'/build.json'));assert(receipt.pass);assert.equal(sha(fs.readFileSync(build+'/fixture.mjs')),receipt.outputs['fixture.mjs']);
for(const p of [new URL(import.meta.url),helper,build+'/build.json',build+'/fixture.mjs'])report.inputs[String(p)]=sha(fs.readFileSync(p));
async function instance(selected){
 const m=await create({print(){},printErr(){}});m._ref_create();m._ref_trace(0);
 const code=m._ref_map(codeBase,0x5000,5),data=m._ref_map(dataBase,0x20000,3);
 const w=new Uint32Array(m.HEAPU8.buffer);w.fill(0xe1a00000,code>>>2,(code+0x5000)>>>2);w.set(words,(code+pc-codeBase)>>>2);
 w[(code+pc+44-codeBase)>>>2]=0xeafffffe;
 if(selected){const dummy={asid:1,pc:0x17000,words:[0xe2522001,0x1afffffd],function:1,complete:true};
 m.countedBlockFactory=()=>({entries:[{...row,function:0,complete:true},dummy],functions:[()=>{throw Error('Semantic decline escaped to generated fallback');},()=>{throw Error('Unused counted fixture loop');}]});
 m.loopBlockFactory=()=>({abi:'counted-pure-arm-loop-v1',entries:[{...dummy,function:0}],functions:[()=>{throw Error('Unused fixture loop');}]});
 m.semanticFactory=env=>{m.geometry=env.geometry;m.shifterCarry=env.shifterCarry;return createIndexedRowFactory([row])(env);};
 assert.equal(m._eka2l1_counted_configure(1),2,m.__countedError);assert.equal(m._eka2l1_loop_configure(1),1,m.__loopError||m.__semanticError);}
 return {m,code,data};
}
function seed(f,{length=20,pattern=0,source=dataBase+0x100,destination=dataBase+0x4000,palette=dataBase+0x8000,control=dataBase+0xa000,counter=0,limit=20,cpsr=0x10}={}) {
 const m=f.m,w=new Uint32Array(m.HEAPU8.buffer);m.HEAPU8.fill(0xa5,f.data,f.data+0x20000);
 for(let i=0;i<8192;i++)m.HEAPU8[f.data+source-dataBase+i]=pattern===0?(i*73+17)&255:pattern===1?0:pattern===2?1:i&1;
 const halves=new Uint16Array(m.HEAPU8.buffer);for(let i=0;i<256;i++)halves[(f.data+palette-dataBase)/2+i]=((i*313)^0x9bcd)&65535;
 m.HEAPU8[f.data+control-dataBase+16]=length;
 m._ref_cpsr(cpsr);for(let i=0;i<16;i++)m._ref_register(i,0x12340000+i);
 for(const [r,v]of [[0,palette],[4,destination],[5,source],[6,counter],[8,control],[12,limit],[15,pc]])m._ref_register(r,v);
 m._ref_irq(0);m._ref_enable_tlb(1);m._ref_generation();m._ref_clear_code();
}
const snap=f=>JSON.parse(f.m.UTF8ToString(f.m._ref_snapshot()));
function compare(a,b,label){const x=snap(a),y=snap(b);for(const key of ['Reg','Reg_usr','Reg_svc','Reg_abort','Reg_undef','Reg_irq','Reg_firq','Spsr','CP15','VFP','ExtReg','Cpsr','Spsr_copy','Mode','Bank','NFlag','ZFlag','CFlag','VFlag','IFFlags','shifter_carry_out','TFlag','NresetSig','NfiqSig','NirqSig','abortSig','NtransSig','bigendSig','syscallSig','fault','fault_address'])assert.deepEqual(y[key],x[key],label+': '+key);
 assert.equal(sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000)),sha(a.m.HEAPU8.subarray(a.data,a.data+0x20000)),label+': RAM');}
let a,b;
try{
 a=await instance(false);b=await instance(true);
 let index=0;
 const seeds=[{}, {pattern:1},{pattern:2},{pattern:3},{length:0},{length:1},{length:255,limit:255},{source:dataBase+0xfff},{destination:dataBase+0x4ffe},{palette:dataBase+0x8f00},{counter:0xffffffff},{pattern:1,counter:0x7ffffffd,limit:0x7fffffff},{pattern:1,counter:0x80000000,limit:0},{destination:dataBase+0xa010},{destination:dataBase+0x100},{destination:dataBase+0x8000}];
 for(const seedValue of seeds)for(const flags of [0,0x10000000,0x20000000,0x80000000,0xf0000000])for(const budget of [1,3,4,10,11,12,21,22,23,219,220,221,2804,2805,4840]){
  const cfg={...seedValue,cpsr:(0x10|flags)>>>0};seed(a,cfg);seed(b,cfg);
  const n=a.m._ref_run(budget),c=b.m._ref_semantic_run(budget);assert.equal(n,budget);assert.equal(c,n);compare(a,b,'case '+index);
  report.cases.push({index:index++,budget,seed:cfg});
 }
 assert(b.m.__semanticCalls>0&&b.m.__semanticInstructions>0);assert.equal(b.m.__semanticError,null);
 const unchanged=(name,prepare=()=>{},budget=110)=>{seed(b);prepare();const before=snap(b),ram=sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000));assert.equal(b.m._ref_semantic_direct(budget,0),0,name);assert.deepEqual(snap(b),before,name);assert.equal(sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000)),ram,name);report.controls.push(name);};
 for(const budget of [0,1,10])unchanged('short-'+budget,()=>{},budget);
 unchanged('wrong-pc',()=>b.m._ref_register(15,pc+4));
 unchanged('wrong-asid',()=>b.m._ref_asid(2));b.m._ref_asid(1);
 unchanged('pending-irq',()=>b.m._ref_irq(1));b.m._ref_irq(0);
 unchanged('source-drift',()=>{b.m.HEAPU8[b.code+pc-codeBase]^=1;b.m._ref_generation();});b.m.HEAPU8[b.code+pc-codeBase]^=1;b.m._ref_generation();b.m._ref_clear_code();
 unchanged('read-denied',()=>b.m._ref_deny(dataBase+0x100,dataBase+0x110,1));b.m._ref_deny(0,0,0);
 unchanged('write-denied',()=>b.m._ref_deny(dataBase+0x4000,dataBase+0x4010,2));b.m._ref_deny(0,0,0);
 unchanged('unaligned-output',()=>b.m._ref_register(4,dataBase+0x4001));
 unchanged('unaligned-palette',()=>b.m._ref_register(0,dataBase+0x8001));
 unchanged('limit-alias',()=>b.m._ref_register(4,dataBase+0xa010));
 unchanged('big-endian',()=>b.m._ref_cpsr(0x210));
 unchanged('final-iteration',()=>b.m._ref_register(6,19));
 seed(a);seed(b);const old=b.data;b.data=b.m._ref_remap(dataBase);assert.notEqual(old,b.data);assert.equal(a.m._ref_run(110),110);assert.equal(b.m._ref_semantic_run(110),110);compare(a,b,'remapped data');report.controls.push('remapped-data');
 seed(b);assert.equal(b.m._ref_semantic_direct(110,0),110);const resolves=b.m._eka2l1_semantic_resolutions();
 for(const[r,v]of [[4,dataBase+0x4000],[5,dataBase+0x100],[6,0],[12,20]])b.m._ref_register(r,v);
 assert.equal(b.m._ref_semantic_direct(110,0),110);assert.equal(b.m._eka2l1_semantic_resolutions(),resolves);report.controls.push('same-generation-cache');
 for(const[r,v]of [[4,dataBase+0x4000],[5,dataBase+0x100],[6,0],[12,20]])b.m._ref_register(r,v);
 b.m._ref_deny(dataBase+0x4000,dataBase+0x4010,2);const before=snap(b),ram=sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000));
 assert.equal(b.m._ref_semantic_direct(110,0),0);assert.deepEqual(snap(b),before);assert.equal(sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000)),ram);report.controls.push('warm-write-revocation');
 b.m._ref_deny(0,0,0);assert.equal(b.m._ref_semantic_direct(110,0),110);report.controls.push('warm-write-restoration');
 seed(b,{pattern:1,counter:0x80000000,limit:0});assert.equal(b.m._ref_semantic_direct(20000,0),11264);report.controls.push('bounded-1024-backedges');
 assert(b.m.__semanticCalls>0);assert.equal(b.m.__semanticError,null);
 report.semantic={calls:b.m.__semanticCalls,instructions:b.m.__semanticInstructions,bindings:b.m.__semanticBindings};report.pass=true;
}catch(e){report.errors.push({message:e.message,stack:e.stack});report.failure={original:a?snap(a):null,candidate:b?snap(b):null,semantic:b?{calls:b.m.__semanticCalls,instructions:b.m.__semanticInstructions,bindings:b.m.__semanticBindings}:null};}
finally{for(const f of[a,b])f?.m._ref_destroy();fs.writeFileSync(out+'/report.json',JSON.stringify(report,null,2)+'\n');}
console.log(JSON.stringify({pass:report.pass,cases:report.cases.length,controls:report.controls,semantic:report.semantic,errors:report.errors}));if(!report.pass)process.exitCode=1;
