import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'eka-policy-test-'));
fs.writeFileSync(path.join(temp, 'eka2l1.html'), '<html><head></head><body></body></html>');
process.env.EKA2L1_WASM_BUILD_DIR = temp;
const {startServer, compilerPolicyFromEnv} = await import('./server.ts');
const servers: any[] = [];
const retired = ['AOT_VERIFY','AOT_DIAGNOSTICS','PROFILE_DETAIL','GUEST_PROFILE','EXIT_CENSUS',
  'AOT_IR_MODE','PREDICATED_LEAVES','LEAF_FEATURES','CODE_COMPARE','ARM_EXCLUSIVE',
  'SPARSE_ROM_LOOKUP','COMPILED_SVC','HOTPATH','THUMB_MEMORY','AOT_HOT','AOT_REGION',
  'AOT_CHAIN','AOT_RAM','EXECUTION_LIMITS','ENTRY_BUDGET','TLB_HASH','MEMORY_CACHE'];
try {
  for (const name of [...retired,'UNSAFE_CODE','MEMORY_IMPL','WATCHDOG_US','ARENA_GROWTH']) delete process.env['EKA2L1_'+name];
  assert.deepEqual(compilerPolicyFromEnv(), {watchdogUs:2000,unsafeCode:3,memoryImpl:2});
  process.env.EKA2L1_UNSAFE_CODE = '0';
  assert.deepEqual(compilerPolicyFromEnv(), {watchdogUs:2000,unsafeCode:0,memoryImpl:0});
  delete process.env.EKA2L1_UNSAFE_CODE;
  for (const value of ['0','1']) {
    process.env.EKA2L1_ARENA_GROWTH=value;
    assert.equal(compilerPolicyFromEnv().arenaGrowth,Number(value));
  }
  for (const value of ['','2','true','-1']) {
    process.env.EKA2L1_ARENA_GROWTH=value;
    assert.throws(compilerPolicyFromEnv,/Invalid arena growth policy/);
  }
  process.env.EKA2L1_ARENA_GROWTH='1';process.env.EKA2L1_MEMORY_IMPL='0';
  assert.throws(compilerPolicyFromEnv,/Invalid arena growth policy/);
  delete process.env.EKA2L1_ARENA_GROWTH;delete process.env.EKA2L1_MEMORY_IMPL;
  for (const name of retired) {
    process.env['EKA2L1_'+name] = '0';
    assert.throws(compilerPolicyFromEnv, /Retired compiler option/);
    delete process.env['EKA2L1_'+name];
  }
  for (const [name, valid, invalid] of [
    ['UNSAFE_CODE',['0','3'],['','1','2','4','-1','3.0']],
    ['MEMORY_IMPL',['0','2'],['','1','3','-1','2.0']],
  ] as const) {
    for (const value of valid) { process.env['EKA2L1_'+name]=value; compilerPolicyFromEnv(); }
    for (const value of invalid) {
      process.env['EKA2L1_'+name]=value;
      assert.throws(compilerPolicyFromEnv, /Invalid .* policy/);
    }
    delete process.env['EKA2L1_'+name];
  }
  const etags = [];
  for (const policy of [{unsafeCode:0,memoryImpl:0},{unsafeCode:3,memoryImpl:0},{unsafeCode:3,memoryImpl:2},
      {unsafeCode:3,memoryImpl:2,arenaGrowth:0},{unsafeCode:3,memoryImpl:2,arenaGrowth:1}]) {
    const {server,port} = await startServer(0,{},undefined,{compilerPolicy:policy}); servers.push(server);
    const url = `http://127.0.0.1:${port}/`;
    const response = await fetch(url), html = await response.text(); etags.push(response.headers.get('etag'));
    for (const value of ['on','off','invalid']) assert.equal(await (await fetch(url+'?counting='+value)).text(), html);
    const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('window.ekaCompilerPolicy ='))!;
    assert.ok(script);
    // Rejected/missing configuration and incorrect readback must prevent startup.
    for (const failure of ['none','missing','rejected','wrong-readback']) {
      let starts=0;
      const values: Record<string,number>={};
      const names=['unsafe_code','memory_impl','arena_growth'];
      const context=vm.createContext({ekaWatchdog:{},window:{},startEmulator:async()=>{++starts;},Module:{
        ...Object.fromEntries(names.flatMap(n=>['configure','report'].map(s=>['_eka2l1_'+n+'_'+s,()=>0]))
          .filter(([name])=>failure!=='missing'||name!=='_eka2l1_memory_impl_configure')),
        ccall:(name:string,_type:string,_types:string[],args:number[])=>{
          const key=name.replace('eka2l1_','').replace(/_(configure|report)$/,'');
          if(name.endsWith('_configure')) {values[key]=args[0];return failure==='rejected'?-1:0;}
          return failure==='wrong-readback'?-1:values[key];
        }
      }});
      vm.runInContext(script,context);
      if(failure==='none') {
        await vm.runInContext('startEmulator()',context);
        assert.equal(starts,1);
        assert.deepEqual(JSON.parse(JSON.stringify(context.window.ekaCompilerPolicy.observed)),policy);
      } else {
        await assert.rejects(vm.runInContext('startEmulator()',context),/Emulator compiler/);
        assert.equal(starts,0);assert.equal(context.window.ekaCompilerPolicy.applied,false);
      }
    }
  }
  assert.equal(new Set(etags).size,etags.length);
  for (const policy of [{irMode:7},{compiledSvc:1},{sparseRom:1},{hotpath:2},{thumbMemory:1},
      {predicatedLeaves:1},{leafFeatures:224},{codeCompare:0},{executionLimits:[512,32,8,0]},
      {unsafeCode:1},{memoryImpl:1},{watchdogUs:0},{arenaGrowth:2},
      {arenaGrowth:1,memoryImpl:0},{arenaGrowth:1,unsafeCode:0}])
    await assert.rejects(startServer(0,{},undefined,{compilerPolicy:policy as any}),/Invalid compiler policy/);
  console.log('PASS compatibility selection, retired controls, startup failure propagation and policy-dependent cache identity');
} finally {
  for (const server of servers) await new Promise<void>(resolve=>server.close(()=>resolve()));
  fs.rmSync(temp,{recursive:true,force:true});
}
