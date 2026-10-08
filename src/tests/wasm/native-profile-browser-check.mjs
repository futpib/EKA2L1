import fs from 'node:fs';
import assert from 'node:assert/strict';
import path from 'node:path';
import {startNativeProfile,snapshotNativeMetadata,decodePositions} from './native-profile.mjs';
import puppeteer from 'puppeteer';
if (!process.argv[2]) throw Error('Usage: node native-profile-browser-check.mjs NEW_OUTPUT');
const output=path.resolve(process.argv[2]);
fs.mkdirSync(output);
const leb=n=>{const r=[];do{let v=n&127;n>>>=7;r.push(n?v|128:v)}while(n);return r};
const section=(id,b)=>[id,...leb(b.length),...b];
// run(n) loops n times, with a load, arithmetic and a backward branch.
const body=[1,1,0x7f,0x41,0,0x21,1,0x02,0x40,0x03,0x40,
 0x20,0,0x45,0x0d,1,0x20,1,0x20,0,0x41,0x7f,0x71,0x28,2,0,
 0x6a,0x41,3,0x6c,0x21,1,0x20,0,0x41,1,0x6b,0x21,0,0x0c,0,
 0x0b,0x0b,0x20,1,0x0b];
const bytes=Uint8Array.from([0,97,115,109,1,0,0,0,
 ...section(1,[1,0x60,1,0x7f,1,0x7f]),...section(3,[1,0]),
 ...section(5,[1,0,1]),...section(7,[1,3,114,117,110,0,0]),
 ...section(10,[1,...leb(body.length),...body])]);
if(!WebAssembly.validate(bytes))throw Error('Invalid probe');
fs.writeFileSync(output+'/probe.wasm',bytes);
const flags=`--perf-prof --perf-prof-path=${output} --logfile=${output}/v8.log`;
const browser=await puppeteer.launch({executablePath:process.env.PUPPETEER_EXECUTABLE_PATH || '/usr/bin/chromium',headless:true,
 args:['--no-sandbox','--disable-dev-shm-usage',`--js-flags=${flags}`]});
let sampler;
try {
 const page=await browser.newPage();const cdp=await page.createCDPSession();
 const version=await cdp.send('Browser.getVersion');
 const system=await browser.target().createCDPSession();
 const pids=(await system.send('SystemInfo.getProcessInfo')).processInfo.filter(p=>p.type==='renderer').map(p=>p.id);
 sampler=await startNativeProfile(output,pids,version);
 const result=await page.evaluate(async data=>{
   const {instance}=await WebAssembly.instantiate(Uint8Array.from(data));
   let calls=0,result=0;const start=performance.now();
   while(performance.now()-start<2500){result=instance.exports.run(10000);calls++}
   return {calls,result};
 },[...bytes]);
 fs.writeFileSync(output+'/result.json',JSON.stringify({flags,version,result},null,2));
 await sampler.stop();
 const metadata=snapshotNativeMetadata(output,version);
 const captured=JSON.parse(fs.readFileSync(output+'/native-metadata.json'));
 assert(captured.found.some(c=>c.name.endsWith('-turbofan')), 'Natural optimized tier was captured');
 for (const code of captured.found) {
   assert.deepEqual(decodePositions(Buffer.from(code.source_positions_hex,'hex')),code.positions);
   for (const pc of code.traps) {
     const position=code.positions.filter(p=>p.native_offset<=pc).at(-1);
     assert(position, 'Every fixture trap has a source position');
     assert.equal(body[position.wasm_offset],0x28, 'Native trapping load maps to the WASM load, not a neighbor');
   }
 }
 assert(captured.found.some(c=>c.traps.length), 'At least one trapping load verified');
 console.log({passed:true,version,result,metadata});
}finally{await sampler?.abort();await browser.close()}
