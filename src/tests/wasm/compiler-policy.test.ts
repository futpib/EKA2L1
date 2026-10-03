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
  for (const name of ['EKA2L1_THUMB_MEMORY','EKA2L1_AOT_IR_MODE','EKA2L1_AOT_EAGER_REGIONS','EKA2L1_TLB_HASH','EKA2L1_CODE_COMPARE','EKA2L1_CODE_LOOKUP','EKA2L1_PREDICATED_LEAVES','EKA2L1_LEAF_FEATURES','EKA2L1_EXECUTION_LIMITS','EKA2L1_UNSAFE_CODE','EKA2L1_OMIT_GUARD_PUBLICATION']) delete process.env[name];
  assert.deepEqual(compilerPolicyFromEnv(), {unsafeCode:3});
  for (const mode of [0,1,2,3,4]) {
    process.env.EKA2L1_CODE_COMPARE = String(mode);
    assert.deepEqual(compilerPolicyFromEnv(), {unsafeCode:3,codeCompare:mode});
  }
  for (const value of ['-1','5','2.0','NaN','']) {
    process.env.EKA2L1_CODE_COMPARE = value;
    assert.throws(compilerPolicyFromEnv, /Invalid exact comparison policy/);
  }
  delete process.env.EKA2L1_CODE_COMPARE;
  for (const mode of [0,1]) {
    process.env.EKA2L1_CODE_LOOKUP = String(mode);
    assert.deepEqual(compilerPolicyFromEnv(), {unsafeCode:3,codeLookup:mode});
  }
  for (const value of ['-1','2','1.0','NaN','']) {
    process.env.EKA2L1_CODE_LOOKUP = value;
    assert.throws(compilerPolicyFromEnv, /Invalid code lookup policy/);
  }
  delete process.env.EKA2L1_CODE_LOOKUP;
  process.env.EKA2L1_AOT_IR_MODE = '16';
  assert.deepEqual(compilerPolicyFromEnv(), {unsafeCode:3,irMode:16});
  process.env.EKA2L1_AOT_IR_MODE = '17';
  assert.throws(compilerPolicyFromEnv, /Invalid compiler policy/);
  delete process.env.EKA2L1_AOT_IR_MODE;
  const responses: {html:string; etag:string|null}[] = [];
  for (const [mode,lookup] of [[0,undefined],[2,undefined],[2,0],[2,1],[3,0],[4,0]]) {
    const {server,port} = await startServer(0, {}, undefined, {compilerPolicy:{irMode:7,eagerRegions:0,tlbHash:1,codeCompare:mode,...(lookup === undefined ? {} : {codeLookup:lookup})}});
    servers.push(server);
    const response = await fetch(`http://127.0.0.1:${port}/`);
    const html = await response.text(); responses.push({html,etag:response.headers.get('etag')});
    const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('window.ekaCompilerPolicy ='))!;
    assert.ok(script);
    const calls: [string,number][] = [];
    const entries = ['ir','eager_regions','tlb_hash','code_compare'].map(n=>'eka2l1_'+n+'_configure');
    if (lookup !== undefined) entries.push('eka2l1_code_lookup_configure');
    const expected = [7,0,1,mode,...(lookup === undefined ? [] : [lookup])];
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
    for (const target of ['code_compare',...(lookup === undefined ? [] : ['code_lookup'])]) for (const failure of ['missing','rejected']) {
      const bad = vm.createContext({window:{},startEmulator:async()=>{throw Error('must not start');},Module:{
        ...Object.fromEntries(entries.filter(n=>failure!=='missing'||!n.includes(target)).map(n=>['_'+n,()=>0])),
        ccall:(name:string)=>name.includes(target)?-1:0
      }});
      vm.runInContext(script,bad);
      await assert.rejects(vm.runInContext('startEmulator()',bad),new RegExp("configuration failed: " + (target === "code_lookup" ? "codeLookup" : "codeCompare")));
      assert.equal(bad.window.ekaCompilerPolicy.applied,false);
    }
  }
  assert.equal(new Set(responses.map(r=>r.etag)).size,responses.length);
  for (const invalid of [-1,5,NaN]) await assert.rejects(startServer(0,{},undefined,{compilerPolicy:{codeCompare:invalid}}),/Invalid compiler policy/);
  for (const invalid of [-1,2,NaN]) await assert.rejects(startServer(0,{},undefined,{compilerPolicy:{codeLookup:invalid}}),/Invalid compiler policy/);
  for (const [envName,key,valid,invalid] of [
    ['EKA2L1_OMIT_GUARD_PUBLICATION','omitGuardPublication',['0','1'],['','2','-1','1.0']],
    ['EKA2L1_UNSAFE_CODE','unsafeCode',['0','1','2','3'],['','4','-1','3.0']],
    ['EKA2L1_PREDICATED_LEAVES','predicatedLeaves',['0','1'],['','2','-1','1.0']],
    ['EKA2L1_LEAF_FEATURES','leafFeatures',['0','1','7','8','15','16','24','31','32','63','64','127','128','255'],['','256','-1','8.0','08']],
    ['EKA2L1_EXECUTION_LIMITS','executionLimits',['512,16,8,512','128,1,0,0','2048,64,16,4096'],['512,0,8,512','127,16,8,512','130,16,8,512','2049,16,8,512','512,65,8,512','512,16,17,512','512,16,8,4097','512,16,8,-1','512,16,8,0,0','0512,16,8,512','512,16,8,NaN','']]
  ] as const) {
    for (const value of valid) {
      process.env[envName] = value;
      assert.deepEqual(compilerPolicyFromEnv(), {unsafeCode:3,[key]:key === 'executionLimits' ? value.split(',').map(Number) : Number(value)});
    }
    for (const value of invalid) { process.env[envName] = value; assert.throws(compilerPolicyFromEnv, /Invalid .* policy/); }
    delete process.env[envName];
  }
  const fusionEtags: (string|null)[] = [];
  for (const policy of [
    ...[0,1].map(thumbMemory=>({thumbMemory,unsafeCode:3,predicatedLeaves:1,leafFeatures:128,executionLimits:[512,16,8,512]})),
    ...[0,1].map(omitGuardPublication=>({omitGuardPublication,unsafeCode:3,predicatedLeaves:1,leafFeatures:128,executionLimits:[512,16,8,512]})),
    ...[0,1,2,3].map(unsafeCode=>({unsafeCode,predicatedLeaves:1,leafFeatures:0,executionLimits:[512,16,8,512]})),
    {predicatedLeaves:1,leafFeatures:8,executionLimits:[512,16,8,512]},
    {predicatedLeaves:1,leafFeatures:32,executionLimits:[512,16,8,512]},
    {predicatedLeaves:1,leafFeatures:64,executionLimits:[512,16,8,512]},
    {predicatedLeaves:1,leafFeatures:128,executionLimits:[512,16,8,512]},
    {predicatedLeaves:1,leafFeatures:15,executionLimits:[1024,32,16,0]},
    {predicatedLeaves:0,leafFeatures:0,executionLimits:[128,1,0,4096]}
  ]) {
    const {server,port}=await startServer(0,{},undefined,{compilerPolicy:policy as any});servers.push(server);
    const response=await fetch(`http://127.0.0.1:${port}/`);fusionEtags.push(response.headers.get('etag'));
    const html=await response.text();
    const script=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('window.ekaCompilerPolicy ='))!;
    const entries=['leaf_predication','leaf_features','execution_limits',...('unsafeCode' in policy?['unsafe_code']:[]),...('omitGuardPublication' in policy?['omit_guard_publication']:[]),...('thumbMemory' in policy?['thumb_memory']:[])];
    const values=[policy.predicatedLeaves,policy.leafFeatures,policy.executionLimits.join(','),...('unsafeCode' in policy?[policy.unsafeCode]:[]),...('omitGuardPublication' in policy?[policy.omitGuardPublication]:[]),...('thumbMemory' in policy?[policy.thumbMemory]:[])];
    for (const failure of ['none','missing-config','reject-config','missing-report','wrong-report']) for (const target of failure==='none'?[0]:entries.map((_,i)=>i)) {
      const configured: Record<string,number[]>={};let starts=0;
      const exports=entries.flatMap(name=>['configure','report'].map(suffix=>'eka2l1_'+name+'_'+suffix));
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
        assert.deepEqual(configured,{eka2l1_leaf_predication_configure:[policy.predicatedLeaves],eka2l1_leaf_features_configure:[policy.leafFeatures],eka2l1_execution_limits_configure:policy.executionLimits,...('unsafeCode' in policy?{eka2l1_unsafe_code_configure:[policy.unsafeCode]}:{}),...('omitGuardPublication' in policy?{eka2l1_omit_guard_publication_configure:[policy.omitGuardPublication]}:{}),...('thumbMemory' in policy?{eka2l1_thumb_memory_configure:[policy.thumbMemory]}:{})});
        await vm.runInContext('startEmulator()',context);assert.equal(starts,2);
      } else {
        await assert.rejects(vm.runInContext('startEmulator()',context),/Emulator compiler (configuration failed|readback unavailable|readback mismatch)/);
        assert.equal(context.window.ekaCompilerPolicy.applied,false);assert.equal(starts,0);
      }
    }
  }
  assert.equal(new Set(fusionEtags).size,fusionEtags.length);
  for(const invalid of [
    {thumbMemory:2},{thumbMemory:-1},{thumbMemory:NaN},
    {omitGuardPublication:2},{omitGuardPublication:-1},{omitGuardPublication:NaN},
    {unsafeCode:4},{unsafeCode:-1},{unsafeCode:NaN},{predicatedLeaves:2},{leafFeatures:256},{leafFeatures:NaN},{leafFeatures:1.5},
    {executionLimits:[512,0,8,512]},{executionLimits:[512,16,8,-1]},{executionLimits:[512,16,8,4097]},
    {executionLimits:[512,16,8]},{executionLimits:'512,16,8,512'}
  ]) await assert.rejects(startServer(0,{},undefined,{compilerPolicy:invalid as any}),/Invalid compiler policy/);
  console.log('PASS scanner, lookup and fusion policies, exact mode/limit readback, rejected configuration and policy-dependent HTML ETags');
} finally {
  for (const server of servers) await new Promise<void>(resolve=>server.close(()=>resolve()));
  fs.rmSync(temp,{recursive:true,force:true});
}
