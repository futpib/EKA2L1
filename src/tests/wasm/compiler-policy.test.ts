import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'eka-policy-test-'));
fs.writeFileSync(path.join(temp, 'eka2l1.html'), '<html><head></head><body></body></html>');
process.env.EKA2L1_WASM_BUILD_DIR = temp;
const { startServer, compilerPolicyFromEnv } = await import('./server.ts');
const servers: any[] = [];
try {
  for (const name of ['EKA2L1_DIVISION_DIGITS','EKA2L1_ENTRY_BUDGET','EKA2L1_SPARSE_ROM_LOOKUP','EKA2L1_ENTRY_ONLY_PRUNING','EKA2L1_COMPILED_SVC','EKA2L1_MEMORY_IMPL','EKA2L1_ARM_MEMORY','EKA2L1_HOTPATH','EKA2L1_THUMB_MEMORY','EKA2L1_AOT_IR_MODE','EKA2L1_AOT_EAGER_REGIONS','EKA2L1_TLB_HASH','EKA2L1_MEMORY_CACHE','EKA2L1_CODE_COMPARE','EKA2L1_CODE_LOOKUP','EKA2L1_PREDICATED_LEAVES','EKA2L1_LEAF_FEATURES','EKA2L1_EXECUTION_LIMITS','EKA2L1_UNSAFE_CODE','EKA2L1_OMIT_GUARD_PUBLICATION']) delete process.env[name];
  assert.deepEqual(compilerPolicyFromEnv(), {predicatedLeaves:1,leafFeatures:224,executionLimits:[512,32,8,512],entryBudget:2,sparseRom:1,compiledSvc:1,hotpath:2,thumbMemory:1,unsafeCode:3,irMode:17});
  for (const mode of [0,2]) {
    process.env.EKA2L1_CODE_COMPARE = String(mode);
    assert.deepEqual(compilerPolicyFromEnv(), {predicatedLeaves:1,leafFeatures:224,executionLimits:[512,32,8,512],entryBudget:2,sparseRom:1,compiledSvc:1,hotpath:2,thumbMemory:1,unsafeCode:3,irMode:17,codeCompare:mode});
  }
  for (const value of ['-1','1','3','4','5','2.0','NaN','']) {
    process.env.EKA2L1_CODE_COMPARE = value;
    assert.throws(compilerPolicyFromEnv, /Invalid exact comparison policy/);
  }
  delete process.env.EKA2L1_CODE_COMPARE;
  for (const name of ['EKA2L1_TLB_HASH','EKA2L1_MEMORY_CACHE','EKA2L1_CODE_LOOKUP','EKA2L1_OMIT_GUARD_PUBLICATION','EKA2L1_DIVISION_DIGITS','EKA2L1_ENTRY_ONLY_PRUNING','EKA2L1_EXECUTION_LIMITS','EKA2L1_ROM_DISPATCH','EKA2L1_SYNCHRONOUS_COMPILATION','EKA2L1_CODE_WRITE_PROTECT','EKA2L1_COMPILED_MEMORY_MISSES','EKA2L1_ROM_CALLS','EKA2L1_ROM_LEAVES','EKA2L1_AOT_EAGER_REGIONS','EKA2L1_SNAKES_N80_NATIVE_RESOLUTION','EKA2L1_ARM_MEMORY']) {
    for (const value of ['0','1']) {
      process.env[name] = value;
      assert.throws(compilerPolicyFromEnv, /Retired compiler option/);
    }
    delete process.env[name];
  }
  for (const mode of [1,2,3,8,9,10,11,12,13,14,15,16,18]) {
    process.env.EKA2L1_AOT_IR_MODE = String(mode);
    assert.throws(compilerPolicyFromEnv, /Invalid compiler policy/);
    await assert.rejects(startServer(0,{},undefined,{compilerPolicy:{irMode:mode}}), /Invalid compiler policy/);
  }
  process.env.EKA2L1_AOT_IR_MODE = '7';
  assert.deepEqual(compilerPolicyFromEnv(), {predicatedLeaves:1,leafFeatures:224,executionLimits:[512,32,8,512],entryBudget:2,sparseRom:1,compiledSvc:1,hotpath:2,thumbMemory:1,unsafeCode:3,irMode:7});
  process.env.EKA2L1_AOT_IR_MODE = '17';
  assert.deepEqual(compilerPolicyFromEnv(), {predicatedLeaves:1,leafFeatures:224,executionLimits:[512,32,8,512],entryBudget:2,sparseRom:1,compiledSvc:1,hotpath:2,thumbMemory:1,unsafeCode:3,irMode:17});
  process.env.EKA2L1_AOT_IR_MODE = '19';
  assert.throws(compilerPolicyFromEnv, /Invalid compiler policy/);
  delete process.env.EKA2L1_AOT_IR_MODE;
  const responses: {html:string; etag:string|null}[] = [];
  for (const mode of [0,2]) {
    const {server,port} = await startServer(0, {}, undefined, {compilerPolicy:{irMode:7,codeCompare:mode}});
    servers.push(server);
    const response = await fetch(`http://127.0.0.1:${port}/`);
    const html = await response.text(); responses.push({html,etag:response.headers.get('etag')});
    const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('window.ekaCompilerPolicy ='))!;
    assert.ok(script);
    const calls: [string,number][] = [];
    const entries = ['ir','code_compare'].map(n=>'eka2l1_'+n+'_configure');
    const expected = [7,mode];
    let starts = 0;
    const context = vm.createContext({window:{}, startEmulator:async()=>{++starts;}, Module:{
      ...Object.fromEntries(entries.map(n=>['_'+n,()=>0])),
      ccall:(name:string,_type:string,_args:string[],values:number[])=>{calls.push([name,values[0]]);return 0;}
    }});
    vm.runInContext(script, context);
    await vm.runInContext('startEmulator()', context);
    assert.deepEqual(calls, entries.map((entry,i)=>[entry,expected[i]]));
    assert.equal(context.window.ekaCompilerPolicy.applied, true);
    await vm.runInContext('startEmulator()', context);
    assert.equal(calls.length,entries.length); assert.equal(starts,2);
    for (const target of ['code_compare']) for (const failure of ['missing','rejected']) {
      const bad = vm.createContext({window:{},startEmulator:async()=>{throw Error('must not start');},Module:{
        ...Object.fromEntries(entries.filter(n=>failure!=='missing'||!n.includes(target)).map(n=>['_'+n,()=>0])),
        ccall:(name:string)=>name.includes(target)?-1:0
      }});
      vm.runInContext(script,bad);
      await assert.rejects(vm.runInContext('startEmulator()',bad),new RegExp("configuration failed: " + "codeCompare"));
      assert.equal(bad.window.ekaCompilerPolicy.applied,false);
    }
  }
  assert.equal(new Set(responses.map(r=>r.etag)).size,responses.length);
  // A session override must affect the served policy and HTML cache identity
  // without changing the server default used by other tabs.
  for (const policy of [{compiledSvc:1}, {compiledSvc:1,watchdogUs:3000}]) {
    const {server,port} = await startServer(0,{},undefined,{compilerPolicy:policy});
    servers.push(server);
    const url = `http://127.0.0.1:${port}/`;
    const policyAt = async (query: string, etag?: string|null) => {
      const response = await fetch(url + query, {headers:etag?{'If-None-Match':etag}:{}});
      assert.equal(response.status,200);
      const html = await response.text();
      const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('window.ekaCompilerPolicy ='))!;
      const context = vm.createContext({window:{},startEmulator:async()=>{}});
      vm.runInContext(script,context);
      return {policy:JSON.parse(JSON.stringify(context.window.ekaCompilerPolicy.requested)),etag:response.headers.get('etag')};
    };
    assert.deepEqual((await policyAt('')).policy,policy);
    const counted = await policyAt('?counting=on');
    assert.deepEqual(counted.policy,{compiledSvc:1});
    const countFree = await policyAt('?counting=off',counted.etag);
    assert.deepEqual(countFree.policy,{compiledSvc:1,watchdogUs:policy.watchdogUs??2000});
    assert.notEqual(counted.etag,countFree.etag);
    assert.deepEqual((await policyAt('')).policy,policy);
    assert.equal((await fetch(url+'?counting=invalid')).status,400);
  }
  for (const invalid of [-1,5,NaN]) await assert.rejects(startServer(0,{},undefined,{compilerPolicy:{codeCompare:invalid}}),/Invalid compiler policy/);
  for (const policy of [{tlbHash:0},{tlbHash:1},{memoryCache:1},{codeLookup:0},{omitGuardPublication:0}]) await assert.rejects(startServer(0,{},undefined,{compilerPolicy:policy as any}),/Invalid compiler policy/);
  for (const [envName,key,valid,invalid] of [
    ['EKA2L1_SPARSE_ROM_LOOKUP','sparseRom',['0','1'],['','2','-1','1.0']],
    ['EKA2L1_ENTRY_BUDGET','entryBudget',['0','2'],['','1','3','-1','1.0']],
    ['EKA2L1_COMPILED_SVC','compiledSvc',['0','1'],['','2','-1','1.0']],
    ['EKA2L1_MEMORY_IMPL','memoryImpl',['0','2'],['','1','3','-1','2.0']],
    ['EKA2L1_HOTPATH','hotpath',['0','2'],['','1','3','4','5','6','7','8','-1','2.0','02']],
    ['EKA2L1_THUMB_MEMORY','thumbMemory',['0','1'],['','2','-1','1.0']],
    ['EKA2L1_UNSAFE_CODE','unsafeCode',['0','3'],['','1','2','4','-1','3.0']],
    ['EKA2L1_PREDICATED_LEAVES','predicatedLeaves',['0','1'],['','2','-1','1.0']],
    ['EKA2L1_LEAF_FEATURES','leafFeatures',['0','32','64','96','128','160','192','224'],['','1','7','8','15','16','24','31','63','65','127','129','159','161','255','256','-1','8.0','08']],
  ] as const) {
    for (const value of valid) {
      process.env[envName] = value;
      assert.deepEqual(compilerPolicyFromEnv(), {predicatedLeaves:1,leafFeatures:224,executionLimits:[512,32,8,512],entryBudget:2,sparseRom:1,compiledSvc:1,hotpath:2,thumbMemory:1,unsafeCode:3,irMode:17,...(key === 'unsafeCode' && value === '0' ? {memoryImpl:0} : {}),[key]:Number(value)});
    }
    for (const value of invalid) { process.env[envName] = value; assert.throws(compilerPolicyFromEnv, /Invalid .* policy/); }
    delete process.env[envName];
  }
  const promotionOptions = [
    ['compiledSvc','compiled_svc'], ['sparseRom','sparse_rom_lookup'],
    ['entryBudget','entry_budget']
  ] as const;
  const fusionEtags: (string|null)[] = [];
  for (const policy of [
    {compiledSvc:0,sparseRom:1,entryBudget:2,predicatedLeaves:1,leafFeatures:128,executionLimits:[512,32,8,512]},
    {compiledSvc:1,sparseRom:0,entryBudget:0,predicatedLeaves:1,leafFeatures:128,executionLimits:[512,32,8,512]},
    ...[0,2].map(memoryImpl=>({memoryImpl,unsafeCode:3,predicatedLeaves:1,leafFeatures:128,executionLimits:[512,32,8,512]})),
    ...[0,2].map(hotpath=>({hotpath,unsafeCode:3,predicatedLeaves:1,leafFeatures:128,executionLimits:[512,32,8,512]})),
    ...[0,1].map(thumbMemory=>({thumbMemory,unsafeCode:3,predicatedLeaves:1,leafFeatures:128,executionLimits:[512,32,8,512]})),
    ...[0,3].map(unsafeCode=>({unsafeCode,predicatedLeaves:1,leafFeatures:0,executionLimits:[512,32,8,512]})),
    {predicatedLeaves:1,leafFeatures:128,executionLimits:[512,32,8,512]},
    ...[32,64,96,160,192,224].map(leafFeatures=>({predicatedLeaves:1,leafFeatures,executionLimits:[512,32,8,512]})),
    {predicatedLeaves:0,leafFeatures:0,executionLimits:[512,32,8,512]}
  ]) {
    const {server,port}=await startServer(0,{},undefined,{compilerPolicy:policy as any});servers.push(server);
    const response=await fetch(`http://127.0.0.1:${port}/`);fusionEtags.push(response.headers.get('etag'));
    const html=await response.text();
    const script=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('window.ekaCompilerPolicy ='))!;
    const selectedPromotion = promotionOptions.filter(([key])=>key in policy);
    const entries=[...selectedPromotion.map(([,entry])=>entry),'leaf_predication','leaf_features','execution_limits',...('unsafeCode' in policy?['unsafe_code']:[]),...('thumbMemory' in policy?['thumb_memory']:[]),...('hotpath' in policy?['hotpath']:[]),...('memoryImpl' in policy?['memory_impl']:[])];
    const values=[...selectedPromotion.map(([key])=>(policy as any)[key]),policy.predicatedLeaves,policy.leafFeatures,policy.executionLimits.join(','),...('unsafeCode' in policy?[policy.unsafeCode]:[]),...('thumbMemory' in policy?[policy.thumbMemory]:[]),...('hotpath' in policy?[policy.hotpath]:[]),...('memoryImpl' in policy?[policy.memoryImpl]:[])];
    for (const failure of ['none','missing-config','reject-config','missing-report','wrong-report']) for (const target of failure==='none'?[0]:entries.map((_,i)=>i)) {
      if(entries[target]==='execution_limits' && ['missing-config','reject-config'].includes(failure)) continue;
      const configured: Record<string,number[]>={};let starts=0;
      const exports=entries.flatMap(name=>(name==='execution_limits'?['report']:['configure','report']).map(suffix=>'eka2l1_'+name+'_'+suffix));
      const absent='eka2l1_'+entries[target]+(failure==='missing-config'?'_configure':'_report');
      const context=vm.createContext({window:{},startEmulator:async()=>{++starts;},Module:{
        ...Object.fromEntries(exports.filter(name=>!(failure.startsWith('missing')&&name===absent)).map(name=>['_'+name,()=>0])),
        ccall:(name:string,_type:string,_args:string[],args:number[])=>{
          const index=entries.findIndex(entry=>name==='eka2l1_'+entry+'_configure'||name==='eka2l1_'+entry+'_report');
          assert.notEqual(index,-1);
          if(name.endsWith('_configure')){configured[name]=[...args];return failure==='reject-config'&&index===target?-1:0;}
          return failure==='wrong-report'&&index===target?'wrong':values[index];
        }
      }});
      vm.runInContext(script,context);
      if(failure==='none') {
        await vm.runInContext('startEmulator()',context);
        assert.equal(context.window.ekaCompilerPolicy.applied,true);
        assert.deepEqual(JSON.parse(JSON.stringify(context.window.ekaCompilerPolicy.observed)),policy);
        assert.deepEqual(configured,{...Object.fromEntries(selectedPromotion.map(([key,entry])=>['eka2l1_'+entry+'_configure',[(policy as any)[key]]])),eka2l1_leaf_predication_configure:[policy.predicatedLeaves],eka2l1_leaf_features_configure:[policy.leafFeatures],...('unsafeCode' in policy?{eka2l1_unsafe_code_configure:[policy.unsafeCode]}:{}),...('thumbMemory' in policy?{eka2l1_thumb_memory_configure:[policy.thumbMemory]}:{}),...('hotpath' in policy?{eka2l1_hotpath_configure:[policy.hotpath]}:{}),...('memoryImpl' in policy?{eka2l1_memory_impl_configure:[policy.memoryImpl]}:{})});
        await vm.runInContext('startEmulator()',context);assert.equal(starts,2);
      } else {
        await assert.rejects(vm.runInContext('startEmulator()',context),/Emulator compiler (configuration failed|readback unavailable|readback mismatch)/);
        assert.equal(context.window.ekaCompilerPolicy.applied,false);assert.equal(starts,0);
      }
    }
  }
  assert.equal(new Set(fusionEtags).size,fusionEtags.length);
  for(const invalid of [
    {divisionDigits:0},{entryOnlyPruning:0},{entryBudget:1},
    ...[[1024,32,8,512],[512,32,4,512],[512,32,16,512],[512,32,8,64],[512,32,8,0],[512,16,8,512]].map(executionLimits=>({executionLimits})),
    {hotpath:-1},{hotpath:1},{hotpath:3},{hotpath:4},{hotpath:7},{hotpath:8},{hotpath:NaN},{hotpath:2.5},
    {memoryImpl:1},{memoryImpl:3},{memoryImpl:-1},{memoryImpl:NaN},
    {thumbMemory:2},{thumbMemory:-1},{thumbMemory:NaN},
    {omitGuardPublication:2},{omitGuardPublication:-1},{omitGuardPublication:NaN},{eagerRegions:0},
    {unsafeCode:1},{unsafeCode:2},{unsafeCode:4},{unsafeCode:-1},{unsafeCode:NaN},{predicatedLeaves:2},{leafFeatures:256},{leafFeatures:NaN},{leafFeatures:1.5},
    {executionLimits:[512,0,8,512]},{executionLimits:[512,16,8,-1]},{executionLimits:[512,16,8,4097]},
    {executionLimits:[512,16,8]},{executionLimits:'512,16,8,512'}
  ]) await assert.rejects(startServer(0,{},undefined,{compilerPolicy:invalid as any}),/Invalid compiler policy/);
  console.log('PASS scanner and fusion policies, retired-option rejection, exact mode/limit readback, rejected configuration and policy-dependent HTML ETags');
} finally {
  for (const server of servers) await new Promise<void>(resolve=>server.close(()=>resolve()));
  fs.rmSync(temp,{recursive:true,force:true});
}
