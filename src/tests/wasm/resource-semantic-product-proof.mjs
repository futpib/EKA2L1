import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';

const names=['build','capture','registry-report','loop-report','semantic-report','input-report','output'];
const {values}=parseArgs({options:Object.fromEntries(names.map(name=>[name,{type:'string'}]))});
for(const name of names)assert(values[name],'Missing --'+name);
const output=path.resolve(values.output);fs.mkdirSync(output,{recursive:false});
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const report={schema:1,pass:false,scope:'Fresh captured original code and pinned original SIS resource. Actual EKA interpreter is executed independently at every <=4840-slot prefix against the selected semantic native frontdoor. Architectural registers/CPSR, complete mutable RAM and charged original counts must match. Fixture allocator guard bytes are explicit diagnostic memory, never a live input-padding policy. No historical catalog/program/Unicorn authority or full-game/MMU publication/performance claim.',inputs:{},cases:[],errors:[]};
const pin=(file,expected)=>{file=path.resolve(file);const bytes=fs.readFileSync(file),digest=sha(bytes);if(expected)assert.equal(digest,expected,'Input changed: '+file);report.inputs[file]=digest;return bytes;};
const load=(file,expected)=>JSON.parse(pin(file,expected));
const source=fileURLToPath(import.meta.url);pin(source);
let original,candidate;
function snapshot(f){const s=JSON.parse(f.m.UTF8ToString(f.m._ref_snapshot()));return {registers17:[...s.Reg,s.Cpsr],memory:f.regions.map(r=>({name:r.name,sha256:sha(f.m.HEAPU8.subarray(r.p,r.p+r.mappedSize))}))};}
try{
 const build=path.resolve(values.build),buildReport=load(path.join(build,'build.json'));assert.equal(buildReport.pass,true);
 const moduleFile=path.join(build,'fixture.mjs');pin(moduleFile,buildReport.outputs['fixture.mjs']);
 const captured=load(values.capture),captureDir=path.dirname(path.resolve(values.capture));assert.equal(captured.schema,'eka-counted-mapped-modules-v1');
 const semanticReport=load(values['semantic-report']),loopReport=load(values['loop-report']),registryReport=load(values['registry-report']);
 for(const row of[semanticReport,loopReport,registryReport])assert.equal(row.pass,true);
 assert.equal(semanticReport.abi,'original-resource-symbol-v1');assert.equal(loopReport.abi,'counted-original-arm-loop-v2');
 assert.equal(loopReport.runtime_module_sha256,semanticReport.runtime_module_sha256);assert.equal(loopReport.source_manifest_sha256,semanticReport.source_manifest_sha256);assert.equal(loopReport.base_registry_sha256,semanticReport.base_registry_sha256);
 assert.equal(registryReport.outputs['registry.mjs'],semanticReport.base_registry_sha256);assert.equal(semanticReport.loop_registry_sha256,loopReport.outputs['loops.mjs']);
 // The generator authenticates the complete capture hierarchy and source spans;
 // require those consumed files to remain exact before crediting its factory.
 for(const[file,digest]of Object.entries(semanticReport.inputs))pin(file,digest);
 assert.equal(semanticReport.inputs[path.resolve(values.capture)],report.inputs[path.resolve(values.capture)],'Factory belongs to another capture');
 const producer=load(path.resolve(captureDir,captured.producer.report.file),captured.producer.report.sha256);assert.equal(producer.pass,true);assert.equal(producer.result.pass,true);for(const name of ['errors','cleanup_errors','changed_sources','forbidden_requests'])assert.deepEqual(producer[name],[]);
 const sourceManifestFile=path.resolve(captureDir,captured.producer.source_manifest.file);const manifestBytes=pin(sourceManifestFile,captured.producer.source_manifest.sha256);assert.equal(sha(manifestBytes),semanticReport.source_manifest_sha256);
 const moduleRows=['skyforce.exe','euser.dll','drtaeabi.dll'].map(name=>{const rows=captured.modules.filter(r=>r.name.toLowerCase()===name&&r.asid===1&&r.process_uid===0xa020d913);assert.equal(rows.length,1,'Original fixture module absent or ambiguous');const row=rows[0],bytes=pin(path.resolve(captureDir,row.file),row.sha256);assert.equal(bytes.length,row.bytes);return {...row,raw:bytes};});
 assert.equal(moduleRows[0].base,0x70000000);
 const entry=semanticReport.entries[0];assert(semanticReport.entries.length>=1&&semanticReport.entries.length<=4);const optional=semanticReport.entries.slice(1);assert(new Set(optional.map(row=>row.pc)).size===optional.length);for(const row of optional)assert([0x8019d81c,0x7002448c,0x70020df8].includes(row.pc)&&row.asid===1);assert.equal(entry.pc,0x70009768);assert.equal(entry.asid,1);
 const inputReportFile=path.resolve(values['input-report']),inputReport=load(inputReportFile);assert.equal(inputReport.pass,true);for(const[file,digest]of Object.entries(inputReport.inputs))pin(file,digest);
 const input=pin(inputReport.resource.file,inputReport.resource.sha256);assert.equal(input.length,692);assert.equal(inputReport.resource.original_slots,1013982);
 const factoryFiles=[['registry.mjs',values['registry-report'],registryReport],['loops.mjs',values['loop-report'],loopReport],['semantic-registry.mjs',values['semantic-report'],semanticReport]];
 const factories=[];for(const[name,receipt,row]of factoryFiles){const file=path.resolve(path.dirname(receipt),name);pin(file,row.outputs[name]);factories.push((await import(pathToFileURL(file))).default);}
 const create=(await import(pathToFileURL(moduleFile))).default;
 async function instance(semantic){const m=await create({print(){},printErr(){}});m._ref_create();const regions=[];
  for(const row of moduleRows){const p=m._ref_map(row.base,row.bytes,5);m.HEAPU8.set(row.raw,p);}
  for(const[name,base,size]of[['object',0x100000,0x6470],['input',0x200000,input.length],['output',0x400000,input.readUInt32LE(0)],['stack',0x2000000,0x10000]]){const mappedSize=((size+4095)&~4095)+8192,p=m._ref_map(base-4096,mappedSize,3);m.HEAPU8.fill(0xa5,p,p+mappedSize);if(name==='object')m.HEAPU8.fill(0,p+4096,p+4096+size);if(name==='input')m.HEAPU8.set(input,p+4096);regions.push({name,base,size,p,mappedSize});}
  m._ref_cpsr(0x10);for(let i=0;i<13;i++)m._ref_register(i,0x12340000+i);for(const[i,v]of[[0,0x100000],[1,0x200000],[2,0x400000],[3,0],[13,0x200ff00],[14,0x70006860],[15,0x700095d4]])m._ref_register(i,v);m._ref_trace(0);m._ref_enable_tlb(1);
  if(semantic){[m.countedBlockFactory,m.loopBlockFactory,m.semanticFactory]=factories;assert.equal(m._eka2l1_counted_configure(1),registryReport.accepted,m.__countedError);assert.equal(m._eka2l1_loop_configure(1),loopReport.accepted,m.__loopError||m.__semanticError);assert.equal(m.__semanticFunctions.length,semanticReport.entries.length);}
  return {m,regions};
 }
 original=await instance(false);candidate=await instance(true);let done=0,index=0;const prefix=[0,1,2,15,128,1024,4840,3,4766,4767,4768,1],total=inputReport.resource.original_slots;
 while(done<total){const budget=Math.min(prefix[index]??4840,total-done),a=original.m._ref_run(budget),b=candidate.m._ref_semantic_run(budget);assert.equal(a,budget);assert.equal(b,a);assert.equal(original.m._ref_fault(),0,'Original fault at '+original.m._ref_fault_address().toString(16));assert.equal(candidate.m._ref_fault(),0,'Selected fault at '+candidate.m._ref_fault_address().toString(16));const expected=snapshot(original),actual=snapshot(candidate);assert.deepEqual(actual,expected,'Original prefix mismatch '+index);done+=a;report.cases.push({index,budget,cumulative:done,...actual,semantic_calls:candidate.m.__semanticCalls,semantic_instructions:candidate.m.__semanticInstructions});index++;}
 assert.equal(report.cases.at(-1).registers17[15],0x70006860,'Original mapped caller return not reached');
 const out=candidate.regions.find(r=>r.name==='output');assert.equal(sha(candidate.m.HEAPU8.subarray(out.p+4096,out.p+4096+out.size)),'1f8b80413e3635eaada7f0547e275b28763ada7d8d938194193b597acf609cc0','Independent original resource output');
 report.semantic={calls:candidate.m.__semanticCalls,instructions:candidate.m.__semanticInstructions,contexts:candidate.m.__semanticBindings,authentications:candidate.m._eka2l1_semantic_authentications(),resolutions:candidate.m._eka2l1_semantic_resolutions()};assert(report.semantic.calls>0&&report.semantic.instructions>0);assert.equal(candidate.m.__semanticError,null);report.pass=true;
}catch(error){report.errors.push({message:error.message,stack:error.stack,semantic_error:candidate?.m.__semanticError});}
finally{for(const f of[original,candidate])f?.m._ref_destroy();for(const[file,digest]of Object.entries(report.inputs))if(sha(fs.readFileSync(file))!==digest){report.pass=false;report.errors.push({message:'Consumed source changed: '+file});}fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');}
console.log(JSON.stringify({pass:report.pass,cases:report.cases.length,semantic:report.semantic,errors:report.errors}));if(!report.pass)process.exitCode=1;
