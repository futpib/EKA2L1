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
const {createListScanFactory}=await import(pathToFileURL(helper));
const out=values.output; assert(out&&!fs.existsSync(out));fs.mkdirSync(out,{recursive:true});
const sha=x=>createHash('sha256').update(x).digest('hex');
const pc=0x10100,codeBase=0x10000,dataBase=0x100000;
const words=[0xe15e0004,0xe24e000c,0x181e5004,0x0a000d92,0xe3520106,0x121c3001,0x0afffff8];
const row={asid:1,pc,words:words.slice(0,4),sources:[{pc,words}]};
const report={pass:false,scope:'Original ARM interpreter versus selected JS list scan; fixture normal RAM, exact CPU flags/registers and memory at bounded instruction prefixes. No product speed claim.',inputs:{},cases:[],controls:[],errors:[]};
const receipt=JSON.parse(fs.readFileSync(build+'/build.json'));assert(receipt.pass);assert.equal(sha(fs.readFileSync(build+'/fixture.mjs')),receipt.outputs['fixture.mjs']);
for(const p of [new URL(import.meta.url),helper,build+'/build.json',build+'/fixture.mjs'])report.inputs[String(p)]=sha(fs.readFileSync(p));
async function instance(selected){
 const m=await create({print(){},printErr(){}});m._ref_create();m._ref_trace(0);
 const code=m._ref_map(codeBase,0x5000,5),data=m._ref_map(dataBase,0x20000,3);
 const w=new Uint32Array(m.HEAPU8.buffer);w.fill(0xe1a00000,code>>>2,(code+0x5000)>>>2);w.set(words,(code+pc-codeBase)>>>2);
 w[(code+pc+0x365c-codeBase)>>>2]=0xeafffffe;
 if(selected){const dummy={asid:1,pc:0x17000,words:[0xe2522001,0x1afffffd],function:1,complete:true};
 m.countedBlockFactory=()=>({entries:[{...row,function:0,complete:true},dummy],functions:[()=>{throw Error('Semantic decline escaped to generated fallback');},()=>{throw Error('Unused counted fixture loop');}]});
 m.loopBlockFactory=()=>({abi:'counted-pure-arm-loop-v1',entries:[{...dummy,function:0}],functions:[()=>{throw Error('Unused fixture loop');}]});
 m.semanticFactory=env=>{m.geometry=env.geometry;m.shifterCarry=env.shifterCarry;return createListScanFactory([row])(env);};
 assert.equal(m._eka2l1_counted_configure(1),2,m.__countedError);assert.equal(m._eka2l1_loop_configure(1),1,m.__loopError||m.__semanticError);}
 return {m,code,data};
}
function seed(f,{length=20,kind=0,flags=0,spacing=16,start=dataBase+0x108,sentinel=dataBase+0x10000,cpsr=0x10,exitKind=null,exitFlags=1}={}) {
 const m=f.m,w=new Uint32Array(m.HEAPU8.buffer);m.HEAPU8.fill(0xa5,f.data,f.data+0x20000);
 for(let i=0;i<length;i++){const addr=start+i*spacing,index=(f.data+addr-dataBase)>>>2;w[index-2]=Array.isArray(kind)?kind[i%kind.length]:kind;w[index-1]=Array.isArray(flags)?flags[i%flags.length]:flags;w[index]=i+1<length?start+(i+1)*spacing:sentinel;}
 if(exitKind!==null&&length){const index=(f.data+start+(length-1)*spacing-dataBase)>>>2;w[index-2]=exitKind;w[index-1]=exitFlags;}
 m._ref_cpsr(cpsr);for(let i=0;i<16;i++)m._ref_register(i,0x12340000+i);m._ref_register(4,sentinel);m._ref_register(14,length?start:sentinel);m._ref_register(15,pc);m._ref_irq(0);m._ref_enable_tlb(1);m._ref_generation();m._ref_clear_code();
}
const snap=f=>JSON.parse(f.m.UTF8ToString(f.m._ref_snapshot()));
function compare(a,b,label){const x=snap(a),y=snap(b);for(const key of ['Reg','Reg_usr','Reg_svc','Reg_abort','Reg_undef','Reg_irq','Reg_firq','Spsr','CP15','VFP','ExtReg','Cpsr','Spsr_copy','Mode','Bank','NFlag','ZFlag','CFlag','VFlag','IFFlags','shifter_carry_out','TFlag','NresetSig','NfiqSig','NirqSig','abortSig','NtransSig','bigendSig','syscallSig','fault','fault_address'])assert.deepEqual(y[key],x[key],label+': '+key);
 assert.equal(sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000)),sha(a.m.HEAPU8.subarray(a.data,a.data+0x20000)),label+': RAM');}
let a,b;
try{
 a=await instance(false);b=await instance(true);
 let index=0;
 const seeds=[{length:2048,kind:[0,1,0x7fffffff,0x80000000,0x80000001,0x80000002,0xffffffff]}, {length:4096,spacing:16}, {}, {length:0},{length:1},{length:513},{length:20,kind:0x80000001,flags:1},{kind:[0,1,0x7fffffff,0x80000000,0x80000001,0x80000002,0xffffffff],flags:[0,2,4,6]},{length:20,exitKind:1},{length:20,spacing:256},{length:20,start:dataBase+0xff8},{length:20,start:dataBase+0xffc}];
 for(const s of seeds)for(const flags of [0,0x10000000,0x20000000,0x80000000,0xf0000000])for(const budget of [1,3,4,6,7,8,13,14,15,139,140,141,3590,3591,4840,7167,7168,10000]){
  const cfg={...s,cpsr:(0x10|flags)>>>0};seed(a,cfg);seed(b,cfg);
  const n=a.m._ref_run(budget),c=b.m._ref_semantic_run(budget);assert.equal(n,budget);assert.equal(c,n);compare(a,b,'case '+index);
  report.cases.push({index:index++,budget,seed:cfg});
 }
 assert(b.m.__semanticCalls>0&&b.m.__semanticInstructions>0);assert.equal(b.m.__semanticError,null);
 // Warm mapping changes and source authentication must still be observed.
 const unchanged=(name,prepare=()=>{},budget=140)=>{seed(b);prepare();const before=snap(b),ram=sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000));assert.equal(b.m._ref_semantic_direct(budget,0),0,name);assert.deepEqual(snap(b),before,name);assert.equal(sha(b.m.HEAPU8.subarray(b.data,b.data+0x20000)),ram);report.controls.push(name);};
 for(const budget of [0,1,6])unchanged('short-'+budget,()=>{},budget);
 unchanged('wrong-pc',()=>b.m._ref_register(15,pc+4));unchanged('wrong-asid',()=>b.m._ref_asid(2));b.m._ref_asid(1);
 unchanged('pending-irq',()=>b.m._ref_irq(1));b.m._ref_irq(0);
 unchanged('source-drift',()=>{b.m.HEAPU8[b.code+pc-codeBase]^=1;b.m._ref_generation();});b.m.HEAPU8[b.code+pc-codeBase]^=1;b.m._ref_generation();b.m._ref_clear_code();
 unchanged('read-denied',()=>b.m._ref_deny(dataBase+0x100,dataBase+0x110,1));b.m._ref_deny(0,0,0);
 unchanged('unaligned-link',()=>b.m._ref_register(14,dataBase+0x109));
 unchanged('cross-page-link',()=>b.m._ref_register(14,dataBase+0x1004));
 seed(a);seed(b);const old=b.data;b.data=b.m._ref_remap(dataBase);assert.notEqual(old,b.data);assert.equal(a.m._ref_run(140),140);assert.equal(b.m._ref_semantic_run(140),140);compare(a,b,'remapped data');report.controls.push('remapped-data');
 unchanged('big-endian',()=>b.m._ref_cpsr(0x210));
 unchanged('thumb',()=>{b.m._ref_cpsr(0x30);new Uint32Array(b.m.HEAPU8.buffer)[(b.m._ref_state()+b.m.geometry[7])>>>2]=1;});
 new Uint32Array(b.m.HEAPU8.buffer)[(b.m._ref_state()+b.m.geometry[7])>>>2]=0;
 unchanged('unmapped-link',()=>b.m._ref_register(14,0xfffffff8));
 // A warmed physical-page entry must not outlive permission or mapping changes.
 seed(b);assert.equal(b.m._ref_semantic_direct(140,0),140);b.m._ref_register(14,dataBase+0x108);
 const resolves=b.m._eka2l1_semantic_resolutions();assert.equal(b.m._ref_semantic_direct(140,0),140);assert.equal(b.m._eka2l1_semantic_resolutions(),resolves);report.controls.push('same-generation-page-cache-hit');
 b.m._ref_register(14,dataBase+0x108);b.m._ref_deny(dataBase+0x100,dataBase+0x110,1);
 const before=snap(b);assert.equal(b.m._ref_semantic_direct(140,0),0);assert.deepEqual(snap(b),before);report.controls.push('warm-permission-revocation');
 b.m._ref_deny(0,0,0);assert.equal(b.m._ref_semantic_direct(140,0),140);report.controls.push('warm-permission-restoration');
 for(const f of[a,b]){seed(f,{length:100});new Uint32Array(f.m.HEAPU8.buffer)[(f.data+0x108+99*16)>>>2]=dataBase+0x108;}
 assert.equal(a.m._ref_run(30000),30000);assert.equal(b.m._ref_semantic_run(30000),30000);compare(a,b,'circular list bounded quantum');report.controls.push('circular-list-bounded-quantum');
 for(const f of[a,b]){seed(f);new Uint32Array(f.m.HEAPU8.buffer)[(f.code+pc+4096-codeBase)>>>2]=0xeafffffe;}
 assert.equal(a.m._ref_run(7),7);assert.equal(b.m._ref_semantic_run(7),7);compare(a,b,'warm original branch link');
 for(const f of[a,b]){f.m._ref_register(14,dataBase+0x108);f.m._ref_register(15,pc);}
 assert.equal(a.m._ref_run(140),140);assert.equal(b.m._ref_semantic_run(140),140);compare(a,b,'initial warm');
 const initial=b.m.__semanticCalls;
 for(const f of[a,b]){f.m._ref_register(14,dataBase+0x108);f.m._ref_register(15,pc);}
 assert.equal(a.m._ref_run(140),140);assert.equal(b.m._ref_semantic_run(140),140);compare(a,b,'warm cached entry');const warm=b.m.__semanticCalls-initial;
 for(const f of[a,b])f.m._ref_register(15,pc+4096);
 assert.equal(a.m._ref_run(2),2);assert.equal(b.m._ref_semantic_run(2),2);compare(a,b,'colliding dispatch');
 const beforeCollision=b.m.__semanticCalls;
 for(const f of[a,b]){f.m._ref_register(14,dataBase+0x108);f.m._ref_register(15,pc);}
 assert.equal(a.m._ref_run(140),140);assert.equal(b.m._ref_semantic_run(140),140);compare(a,b,'after L1 collision');
 report.admission={initial,warm,after_collision:b.m.__semanticCalls-beforeCollision};
 for(const f of[a,b])f.m._ref_register(15,pc+4096);
 assert.equal(a.m._ref_run(2),2);assert.equal(b.m._ref_semantic_run(2),2);
 const linkedBefore=b.m.__semanticCalls;
 for(const f of[a,b]){f.m._ref_register(14,dataBase+0x108);f.m._ref_register(2,0);f.m._ref_register(12,0);f.m._ref_register(15,pc+16);}
 assert.equal(a.m._ref_run(143),143);assert.equal(b.m._ref_semantic_run(143),143);compare(a,b,'linked entry after L1 collision');
 report.admission.linked_after_collision=b.m.__semanticCalls-linkedBefore;assert(report.admission.linked_after_collision>0,"Exact decoded-entry recovery must execute the helper");

 report.controls.push('linked-entry-after-L1-collision');
 report.semantic={calls:b.m.__semanticCalls,instructions:b.m.__semanticInstructions,bindings:b.m.__semanticBindings};report.pass=true;
}catch(e){report.errors.push({message:e.message,stack:e.stack});report.failure={original:a?snap(a):null,candidate:b?snap(b):null,semantic:b?{calls:b.m.__semanticCalls,instructions:b.m.__semanticInstructions,bindings:b.m.__semanticBindings}:null};}
finally{for(const f of[a,b])f?.m._ref_destroy();fs.writeFileSync(out+'/report.json',JSON.stringify(report,null,2)+'\n');}
console.log(JSON.stringify({pass:report.pass,cases:report.cases.length,controls:report.controls,semantic:report.semantic,errors:report.errors}));if(!report.pass)process.exitCode=1;
