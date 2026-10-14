import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import puppeteer from 'puppeteer';
import {allocationResidency} from './allocation-residency.mjs';

const [build,out]=process.argv.slice(2);
if(!build || !out)throw Error('Usage: node allocation-profiler-probe.mjs FIXTURE_BUILD NEW_OUTPUT');
fs.mkdirSync(out);
const server=http.createServer((req,res)=>{
 const name=new URL(req.url,'http://localhost').pathname.slice(1)||'allocation-profiler-fixture.html';
 if(!/^[\w.-]+$/.test(name)||!fs.existsSync(path.join(build,name))){res.writeHead(404);res.end();return;}
 res.setHeader('Cross-Origin-Opener-Policy','same-origin');res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
 res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':name.endsWith('.js')?'text/javascript':'text/html');
 fs.createReadStream(path.join(build,name)).pipe(res);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
try {
 const page=await browser.newPage(),errors=[];
 page.on('pageerror',e=>{errors.push(String(e));console.error(String(e));});
 page.on('console',m=>{if(m.type()==='error')console.error(m.text());});
 await page.goto('http://127.0.0.1:'+server.address().port);
 try{await page.waitForFunction(()=>globalThis.profiles?.length===5);}catch(error){fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify({error:String(error),errors,stages:await page.evaluate(()=>globalThis.profiles||[])},null,2));throw error;}
 const stages=await page.evaluate(()=>profiles);
 fs.writeFileSync(path.join(out,'stages.json'),JSON.stringify(stages,null,2));
 const baseline=stages[0].profile;
 assert(stages[1].profile.live_bytes-baseline.live_bytes>=31*1024*1024,'All large allocation APIs, including mmap, must be observed');
 assert(stages[2].profile.live_bytes-baseline.live_bytes>=33*1024*1024);
 assert.equal(stages[3].profile.live_bytes,baseline.live_bytes,'Free and munmap must release every tracked allocation');
 assert.equal(stages[4].profile.live_bytes,baseline.live_bytes,'Repeated shuffled small frees must restore the baseline');
 const exported=await page.evaluate(()=>{
  const before=JSON.parse(Module.ccall('eka2l1_allocation_profile_report','string',[],[]));
  const pointer=Module._malloc(3*1024*1024);
  const held=JSON.parse(Module.ccall('eka2l1_allocation_profile_report','string',[],[]));
  Module._free(pointer);
  const after=JSON.parse(Module.ccall('eka2l1_allocation_profile_report','string',[],[]));
  return {before,held,after};
 });
 assert(exported.held.live_bytes-exported.before.live_bytes>=3*1024*1024,'Exported JS malloc must be attributed');
 assert.equal(exported.after.live_bytes,exported.before.live_bytes);
 const control=await page.evaluate(()=>{
  const pointer=Module._malloc(8*1024*1024);HEAPU8.fill(0x5a,pointer,pointer+4*1024*1024);
  return {pointer,capacity:HEAPU8.buffer.byteLength,profile:JSON.parse(Module.ccall('eka2l1_allocation_profile_report','string',[],[]))};
 });
 const system=await browser.target().createCDPSession();
 const processes=(await system.send('SystemInfo.getProcessInfo')).processInfo.map(p=>{
  let pss=0;try{pss=Number(fs.readFileSync(`/proc/${p.id}/smaps_rollup`,'utf8').match(/^Pss:\s+(\d+)/m)[1]);}catch{}
  return {...p,pssKiB:pss};
 });
 const residency=await allocationResidency(page,processes,control.capacity,control.profile.large_allocations);
 assert.equal(residency.marker_verified,true);
 const owned=residency.allocations.find(a=>a.pointer===control.pointer);
 assert(owned.whole_page_resident_bytes>=4*1024*1024-8192,'Written pages must be resident in the identified allocation');
 await page.evaluate(p=>Module._free(p),control.pointer);
 assert.equal(errors.length,0);
 fs.writeFileSync(path.join(out,'report.json'),JSON.stringify({browser:await browser.version(),errors,stages,exported,residency},null,2));
 console.log(JSON.stringify({verified:true,marker_verified:residency.marker_verified,control_resident_bytes:owned.whole_page_resident_bytes,baseline_bytes:baseline.live_bytes,peak_bytes:stages.at(-1).profile.peak_bytes,allocations:stages.at(-1).profile.allocations,unknown_frees:stages.at(-1).profile.unknown_frees}));
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
