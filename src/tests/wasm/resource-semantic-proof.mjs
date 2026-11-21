import {parseArgs} from 'node:util';import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {pathToFileURL,fileURLToPath} from 'node:url';
const {values}=parseArgs({options:Object.fromEntries(['build','catalog','program','input','oracle','output'].map(name=>[name,{type:'string'}]).concat([['controls',{type:'boolean',default:false}]]))});
for(const name of ['build','catalog','program','input','oracle','output'])assert(values[name],'Missing --'+name);
const build=path.resolve(values.build),output=path.resolve(values.output);assert(!fs.existsSync(output),'Output must be absent');fs.mkdirSync(output,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex'),load=f=>JSON.parse(fs.readFileSync(f));
const catalogFile=path.resolve(values.catalog),programDir=path.resolve(values.program),inputFile=path.resolve(values.input),oracleFile=path.resolve(values.oracle);
const receipt=load(build+'/build.json'),catalog=load(catalogFile),meta=load(programDir+'/program.json'),oracle=load(oracleFile),input=fs.readFileSync(inputFile);assert(receipt.pass&&oracle.pass);assert.equal(sha(fs.readFileSync(build+'/fixture.mjs')),receipt.outputs['fixture.mjs']);assert.equal(meta.catalog_sha256,sha(fs.readFileSync(catalogFile)));assert.equal(sha(input),oracle.input.sha256);
const helper=fileURLToPath(new URL('../../emu/wasm-js/ari-lz-semantics.mjs',import.meta.url));
const report={schema:1,pass:false,controls:[],scope:'Actual original EKA sparse CPU vs selected semantic native loop frontdoor. Authentic original code/input, exact architectural CPU and complete mutable RAM/counts at realistic <=4840-slot continuations. RAM resolver is fixture-owned normal RAM, not product MMU owner publication proof. Original guard overreads remain explicit. No performance or full-game claim.',pins:{},cases:[],errors:[]};for(const f of[catalogFile,programDir+'/program.json',programDir+'/program.mjs',programDir+'/report.json',oracleFile,inputFile,helper,build+'/fixture.mjs',build+'/build.json',fileURLToPath(import.meta.url)])report.pins[f]=sha(fs.readFileSync(f));
const create=(await import(pathToFileURL(build+'/fixture.mjs'))).default,createProgram=(await import(pathToFileURL(programDir+'/program.mjs'))).default,{createOriginalSymbolFactory} = await import(pathToFileURL(helper));
const programProof=load(programDir+'/report.json');assert(programProof.pass);for(const[name,digest]of Object.entries(programProof.outputs))assert.equal(sha(fs.readFileSync(path.join(programDir,name))),digest,'Emitted program identity');
const accepted=new Map(meta.emitted.accepted.map(r=>[r.candidate,r.export])),model=meta.blocks.find(r=>r.pc===0x11298);assert(model);
const nodes=catalog.nodes.toSorted((a,b)=>a.pc-b.pc),sources=[];for(const n of nodes){const last=sources.at(-1);if(last&&last.pc+last.words.length*4===n.pc)last.words.push(n.opcode);else sources.push({pc:n.pc,words:[n.opcode]});}assert(sources.length<=64);
let original,candidate;
async function instance(semantic){
 const m=await create({print(){},printErr(){}});m._ref_create();const regions=[],code=[];
 for(const row of catalog.segments){const raw=fs.readFileSync(row.file);assert.equal(sha(raw),row.sha256);const p=m._ref_map(row.base,raw.length,5);m.HEAPU8.set(raw,p);code.push({...row,p});}
 for(const[name,base,size]of[['object',0x100000,0x6470],['input',0x200000,input.length],['output',0x400000,input.readUInt32LE(0)],['stack',0x2000000,0x10000]]){const mappedSize=((size+4095)&~4095)+8192,p=m._ref_map(base-4096,mappedSize,3);m.HEAPU8.fill(0xa5,p,p+mappedSize);if(name==='object')m.HEAPU8.fill(0,p+4096,p+4096+size);if(name==='input')m.HEAPU8.set(input,p+4096);regions.push({name,base,size,p,mappedSize});}
 m._ref_cpsr(0x10);for(let i=0;i<13;i++)m._ref_register(i,0x12340000+i);for(const[i,v]of[[0,0x100000],[1,0x200000],[2,0x400000],[3,0],[13,0x200ff00],[14,oracle.return_pc],[15,catalog.entry]])m._ref_register(i,v);m._ref_trace(0);m._ref_enable_tlb(1);
 if(semantic){m.countedBlockFactory=imports=>{const f=createProgram(imports);return {entries:meta.blocks.map((r,i)=>({asid:1,pc:r.pc,words:meta.candidates[r.full].words,function:i,complete:true})),functions:meta.blocks.map(r=>f[accepted.get(r.full)])};};
 m.loopBlockFactory=imports=>{const f=createProgram(imports);return {abi:'counted-original-arm-loop-v2',entries:[{asid:1,pc:model.pc,words:meta.candidates[model.full].words,function:0,complete:true}],functions:[cpu=>{const full=imports.env.loop_budget(cpu)>=5,n=f[accepted.get(full?model.full:model.single)](cpu);return n|(full?65536:0);} ]};};
 const semantic=createOriginalSymbolFactory([{asid:1,pc:0x11768,words:[0xe1a00007,0xebfffeed],sources,codeBase:0x8000}]);m.semanticFactory=env=>{m.__fixtureGeometry=env.geometry;return semantic(env);};
 assert.equal(m._eka2l1_counted_configure(1),meta.blocks.length,m.__countedError);assert.equal(m._eka2l1_loop_configure(1),1,m.__loopError||m.__semanticError);assert.equal(m.__semanticFunctions.length,1);}
 return {m,regions,code};
}
function snapshot(f){const state=JSON.parse(f.m.UTF8ToString(f.m._ref_snapshot()));return {state,registers17:[...state.Reg,state.Cpsr],memory:f.regions.map(r=>({name:r.name,sha256:sha(f.m.HEAPU8.subarray(r.p+4096-256,r.p+4096+r.size+256))}))};}
try{
 if(values.controls){ candidate=await instance(true);const m=candidate.m;
 assert.equal(m._ref_run(156721),156721);assert.equal(snapshot(candidate).state.Reg[15],0x11768);
 assert.equal(m._eka2l1_counted_configure(1),meta.blocks.length);assert.equal(m._eka2l1_loop_configure(1),1);
 m._ref_save_seed();const saved=candidate.regions.map(r=>({r,bytes:m.HEAPU8.slice(r.p,r.p+r.mappedSize)}));
 const reset=()=>{m._ref_restore_seed();for(const {r,bytes}of saved)m.HEAPU8.set(bytes,r.p);};
 const unchanged=(name,prepare=()=>{},budget=4840)=>{reset();prepare();const before=snapshot(candidate);const result=m._ref_semantic_direct(budget,0);assert.equal(result,0,name+' must refuse');assert.deepEqual(snapshot(candidate),before,name+' changed CPU or RAM');report.controls.push({name,result,before_effect_refusal:true});};
 for(const budget of [0,1,883])unchanged('short-budget-'+budget,()=>{},budget);
 unchanged('pending-unmasked-IRQ',()=>m._ref_irq(1));
 unchanged('Thumb',()=>{m._ref_cpsr(snapshot(candidate).state.Cpsr|32);new Uint32Array(m.HEAPU8.buffer)[(m._ref_state()+m.__fixtureGeometry[7])>>>2]=1;});
 unchanged('big-endian',()=>m._ref_cpsr(snapshot(candidate).state.Cpsr|512));
 unchanged('wrong-ASID',()=>m._ref_asid(2));m._ref_asid(1);
 unchanged('wrong-entry-PC',()=>m._ref_register(15,0x1176c));
 const checkPositive=name=>{reset();const count=m._ref_semantic_direct(884,0);assert.equal(count,884,name);const state=snapshot(candidate);report.controls.push({name,count,state,authentications:m._eka2l1_semantic_authentications(),resolutions:m._eka2l1_semantic_resolutions()});return state;};
 const positive=checkPositive('fresh-exact-budget');const auth=m._eka2l1_semantic_authentications(),resolutions=m._eka2l1_semantic_resolutions();
 assert.deepEqual(checkPositive('unchanged-key-cache-hit'),positive);assert.equal(m._eka2l1_semantic_authentications(),auth);assert.equal(m._eka2l1_semantic_resolutions(),resolutions);
 m._ref_generation();assert.deepEqual(checkPositive('changed-mapping-generation'),positive);assert(m._eka2l1_semantic_authentications()>auth&&m._eka2l1_semantic_resolutions()>resolutions);
 const code=candidate.code[0],offset=0x11768-code.base,old=m.HEAPU8[code.p+offset];m.HEAPU8[code.p+offset]^=1;m._ref_generation();unchanged('changed-authenticated-source');m.HEAPU8[code.p+offset]=old;m._ref_generation();assert.deepEqual(checkPositive('restored-source-retry'),positive);
 unchanged('object-write-permission-denied',()=>m._ref_deny(0x100100,0x100101,2));m._ref_deny(0,0,0);
 const object=candidate.regions.find(r=>r.name==='object'),cursor=new DataView(m.HEAPU8.buffer).getUint32(object.p+4096+0x64,true);
 unchanged('input-read-permission-denied',()=>m._ref_deny(cursor,cursor+1,1));m._ref_deny(0,0,0);
 assert.deepEqual(checkPositive('permissions-restored-retry'),positive);
 const oldPointer=object.p;object.p=m._ref_remap(object.base-4096);assert.notEqual(object.p,oldPointer);for(const row of saved)if(row.r===object)row.r.p=object.p;
 assert.deepEqual(checkPositive('new-host-mapping-retry'),positive);
 // Native entry must not provide a RAM pointer outside an authenticated invocation.
 assert.equal(m._eka2l1_semantic_resolve(m._ref_state(),0x100000,4,1),0);report.controls.push({name:'out-of-invocation-resolver',result:0});
 report.semantic={calls:m.__semanticCalls,instructions:m.__semanticInstructions};report.pass=true;
}else{
 original=await instance(false);candidate=await instance(true);const total=oracle.snapshots.reduce((n,r)=>n+r.unicorn_instruction_visits,0);assert.equal(total,1013982);let done=0,index=0;
 const prefix=[0,1,2,15,128,1024,4840,3,4766,4767,4768,1];
 while(done<total){const budget=Math.min(prefix[index]??4840,total-done);const n=original.m._ref_run(budget),c=candidate.m._ref_semantic_run(budget);const a=snapshot(original),b=snapshot(candidate);assert.equal(n,budget,'Original requested count');assert.equal(c,n,'Selected requested count');assert.equal(original.m._ref_fault(),0);assert.equal(candidate.m._ref_fault(),0,'Selected fault');assert.deepEqual(b.registers17,a.registers17,'Architectural registers at prefix '+index);assert.deepEqual(b.memory,a.memory,'Complete mutable memory at prefix '+index);report.cases.push({index,budget,cumulative:done+n,registers17:b.registers17,memory:b.memory,semantic_calls:candidate.m.__semanticCalls,semantic_instructions:candidate.m.__semanticInstructions});done+=n;index++;}
 const final=snapshot(candidate);assert.deepEqual(final.registers17,oracle.snapshots.at(-1).registers17);assert.deepEqual(final.memory,oracle.snapshots.at(-1).memory.map(({name,sha256})=>({name,sha256})));assert(candidate.m.__semanticCalls>0&&candidate.m.__semanticInstructions>0,'No actual positive semantic bridge execution');assert.equal(candidate.m.__semanticError,null);report.semantic={calls:candidate.m.__semanticCalls,instructions:candidate.m.__semanticInstructions,contexts:candidate.m.__semanticBindings,authentications:candidate.m._eka2l1_semantic_authentications(),resolutions:candidate.m._eka2l1_semantic_resolutions()};report.pass=true;
 }
}catch(e){report.errors.push({message:e.message,stack:e.stack,semantic_error:candidate?.m.__semanticError,loop_error:candidate?.m.__loopError,counted_error:candidate?.m.__countedError});try{report.failure={...(original?{original:snapshot(original)}:{}),...(candidate?{candidate:snapshot(candidate)}:{})};}catch{}}
finally{for(const f of[original,candidate])f?.m._ref_destroy();for(const[f,h]of Object.entries(report.pins))assert.equal(sha(fs.readFileSync(f)),h,'Source changed');fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');}
console.log(JSON.stringify({pass:report.pass,cases:report.cases.length,controls:report.controls.length,semantic:report.semantic,errors:report.errors}));if(!report.pass)process.exitCode=1;
