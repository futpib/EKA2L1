import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const puppeteer=createRequire(new URL('../../wasm/package.json', import.meta.url))('puppeteer');
const [buildArg, outputArg] = process.argv.slice(2);
if (!buildArg || !outputArg) throw Error('Usage: node check.mjs TEST_BUILD NEW_OUTPUT');
const build=path.resolve(buildArg), root=path.resolve(outputArg);
fs.mkdirSync(root);
const server=http.createServer((req,res)=>{
 res.setHeader('Cross-Origin-Opener-Policy','same-origin');res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
 if(req.url==='/'){res.setHeader('Content-Type','text/html');res.end('<script>var Module={noInitialRun:true,onRuntimeInitialized(){globalThis.ready=true;},print:console.log,printErr:console.error};</script><script src="/test_aot_wasm.js"></script>');return;}
 const name=path.basename(req.url);if(!/^test_aot_wasm\.(js|wasm)$/.test(name)){res.writeHead(404);res.end();return;}
 res.setHeader('Content-Type',name.endsWith('wasm')?'application/wasm':'application/javascript');fs.createReadStream(path.join(build,name)).pipe(res);
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;
try {
 browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
 const page=await browser.newPage();page.on('pageerror',e=>console.error(e));
 await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>globalThis.ready,{timeout:120000});
 const rows=[];
 for(const [entry,variant,expectedKind] of [['_tile_row_benchmark',0,'arm_dispatch_call'],['_tile_row_benchmark',1,'arm_indirect_call'],['_tile_add_row_benchmark',1,'arm_indirect_miss_call'],['_tile_sub_row_benchmark',1,'arm_indirect_miss_call']]) {
  const result=await page.evaluate(({entry,variant})=>{
   Module._eka2l1_fusion_reset();const status=Module[entry](variant,100);
   const p=Module._eka2l1_fusion_report();const bytes=new Uint8Array(wasmMemory.buffer);let e=p;while(bytes[e])e++;
   return {status,census:JSON.parse(new TextDecoder().decode(new Uint8Array(bytes.subarray(p,e))))};
  },{entry,variant});
  assert.equal(result.status,0);
  const totals={};for(const s of result.census.sites)totals[s.kind]=(totals[s.kind]||0)+s.count;
  assert.equal(totals[expectedKind],1600);
  assert.equal(Object.entries(totals).filter(([k])=>k.endsWith('_call')).reduce((s,[k,n])=>s+n,0),1600);
  if(expectedKind==='arm_indirect_call')assert.equal(totals.arm_indirect_return,1600);
  assert.equal(result.census.region_entries,result.census.region_returns);
  rows.push({entry,variant,expectedKind,totals,...result});console.log(JSON.stringify({entry,variant,totals,regions:result.census.region_entries}));
 }
 fs.writeFileSync(root+'/counter-check.json',JSON.stringify(rows,null,2));
}finally {await browser?.close();server.close();}
