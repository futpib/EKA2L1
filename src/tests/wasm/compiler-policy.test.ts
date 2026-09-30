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
  for (const name of ['EKA2L1_AOT_IR_MODE','EKA2L1_AOT_EAGER_REGIONS','EKA2L1_TLB_HASH','EKA2L1_CODE_COMPARE']) delete process.env[name];
  assert.equal(compilerPolicyFromEnv(), undefined);
  for (const mode of [0,1,2]) {
    process.env.EKA2L1_CODE_COMPARE = String(mode);
    assert.deepEqual(compilerPolicyFromEnv(), {codeCompare:mode});
  }
  for (const value of ['-1','3','2.0','NaN','']) {
    process.env.EKA2L1_CODE_COMPARE = value;
    assert.throws(compilerPolicyFromEnv, /Invalid exact comparison policy/);
  }
  const responses: {html:string; etag:string|null}[] = [];
  for (const mode of [0,2]) {
    const {server,port} = await startServer(0, {}, undefined, {compilerPolicy:{irMode:7,eagerRegions:0,tlbHash:1,codeCompare:mode}});
    servers.push(server);
    const response = await fetch(`http://127.0.0.1:${port}/`);
    const html = await response.text(); responses.push({html,etag:response.headers.get('etag')});
    const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('window.ekaCompilerPolicy ='))!;
    assert.ok(script);
    const calls: [string,number][] = [];
    const entries = ['ir','eager_regions','tlb_hash','code_compare'].map(n=>'eka2l1_'+n+'_configure');
    let starts = 0;
    const context = vm.createContext({window:{}, startEmulator:async()=>{++starts;}, Module:{
      ...Object.fromEntries(entries.map(n=>['_'+n,()=>0])),
      ccall:(name:string,_type:string,_args:string[],values:number[])=>{calls.push([name,values[0]]);return 0;}
    }});
    vm.runInContext(script, context);
    await vm.runInContext('startEmulator()', context);
    assert.deepEqual(calls, entries.map((entry,i)=>[entry,[7,0,1,mode][i]]));
    assert.equal(context.window.ekaCompilerPolicy.applied, true);
    await vm.runInContext('startEmulator()', context);
    assert.equal(calls.length,4); assert.equal(starts,2);
    for (const failure of ['missing','rejected']) {
      const bad = vm.createContext({window:{},startEmulator:async()=>{throw Error('must not start');},Module:{
        ...Object.fromEntries(entries.filter(n=>failure!=='missing'||!n.includes('code_compare')).map(n=>['_'+n,()=>0])),
        ccall:(name:string)=>name.includes('code_compare')?-1:0
      }});
      vm.runInContext(script,bad);
      await assert.rejects(vm.runInContext('startEmulator()',bad),/configuration failed: codeCompare/);
      assert.equal(bad.window.ekaCompilerPolicy.applied,false);
    }
  }
  assert.notEqual(responses[0].etag,responses[1].etag);
  for (const invalid of [-1,3,NaN]) await assert.rejects(startServer(0,{},undefined,{compilerPolicy:{codeCompare:invalid}}),/Invalid compiler policy/);
  console.log('PASS scanner policy validation, applied-mode checks, rejected configuration, and policy-dependent HTML ETags');
} finally {
  for (const server of servers) await new Promise<void>(resolve=>server.close(()=>resolve()));
  fs.rmSync(temp,{recursive:true,force:true});
}
