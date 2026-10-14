import fs from 'node:fs';
import path from 'node:path';
import {PNG} from 'pngjs';
import os from 'node:os';

import assert from 'node:assert/strict';
import puppeteer from 'puppeteer';
const game=process.argv[2], out=process.argv[3];
const games=JSON.parse(fs.readFileSync(new URL('./games.json',import.meta.url)));
const entry=games.find(g=>g.id===game);
if (!entry || !out || !process.argv[4]) throw Error('Usage: node memory-access-profile.mjs GAME NEW_OUTPUT PROFILE_SERVER_URL');
fs.mkdirSync(out);
const assetDir=process.env.EKA2L1_ASSET_DIR || '/tmp/eka2l1-serve';
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--host-resolver-rules=MAP claude-laptop.lan 127.0.0.1','--no-sandbox','--disable-dev-shm-usage','--use-gl=angle','--use-angle=vulkan','--enable-features=Vulkan','--enable-gpu','--ignore-gpu-blocklist','--disable-background-timer-throttling']});
const errors=[],log=fs.createWriteStream(out+'/browser.log');
let status=async()=>null;
try {
const page=await browser.newPage(); await page.setViewport({width:900,height:800});page.setDefaultTimeout(600000);
page.on('console',m=>{log.write(m.text()+'\n');if(/Access violation|KERN-EXEC|ABORT:|panicked with category:/.test(m.text()))errors.push(m.text())});
page.on('pageerror',e=>errors.push(String(e)));
await page.evaluateOnNewDocument(()=>{window.ekaTransientSession=true;});
await page.goto(new URL('?game=custom',process.argv[4]).href,{waitUntil:'domcontentloaded'});
await page.waitForFunction(()=>window.Module?.calledRun);
assert.equal(await page.evaluate(()=>typeof Module._eka2l1_memory_access_profile_report),'function',
 'Build with EKA2L1_MEMORY_ACCESS_PROFILE=ON before capturing');
assert.equal(await page.evaluate(t=>Module._eka2l1_route_configure(t),6000000),0);
for(const [selector,file] of [['#rom-file','SYM.ROM'],['#rpkg-file','SYM.RPKG'],['#sis-file',entry.ngage?.runtime.file ?? entry.file]])await(await page.$(selector)).uploadFile(path.join(assetDir,file));
await page.$eval('#app-name',(e,v)=>e.value=v,entry.uid);
await page.evaluate(entryUrl=>{window.measurementLaunch=startEmulator(entryUrl)},entry.ngage?'/preload/games/'+game+'/ngage':undefined);
await page.waitForFunction(()=>window._gameRunning);
const shot=async name=>{await page.screenshot({path:out+'/'+name+'.png'});fs.appendFileSync(out+'/route-times.jsonl',JSON.stringify({name,...await status()})+'\n');fs.writeFileSync(out+'/'+name+'-profile.json',JSON.stringify(await profile()));return out+'/'+name+'.png'};
status=()=>page.evaluate(()=>({ms:performance.now(),guest:Module._eka2l1_guest_time_us(),frames:Module._eka2l1_presentations(),phase:Module._eka2l1_route_phase(),diskScans:window.diskScans?.length||0,inputs:Module._eka2l1_input_consumed(),monitor:JSON.parse(Module.ccall('eka2l1_monitor_report','string',[],[]))}));
const step=async(us)=>{const s=await status();assert.equal(s.phase,1);assert.equal(await page.evaluate(t=>Module._eka2l1_route_step_to(t),s.guest+us),0);await page.waitForFunction(()=>Module._eka2l1_route_phase()===1);return status()};
const requireBoatWithoutHelp=name=>{
 const png=PNG.sync.read(fs.readFileSync(out+'/'+name+'.png'));
 let pale=0;for(let y=600;y<750;y++)for(let x=100;x<800;x++){const i=(y*png.width+x)*4;if(png.data[i]>190&&png.data[i+1]>210&&png.data[i+2]>210)pale++;}
 assert(pale<30000,'Hooked On tutorial obscures the measured boat scene');
};
const profile=()=>page.evaluate(()=>JSON.parse(Module.ccall('eka2l1_memory_access_profile_report','string',[],[])));
const measure=async(label,seconds=10)=>{
 await shot(label+'-start');
 if(label.startsWith('costa-rica-stationary-tutorial-dismissed'))requireBoatWithoutHelp(label+'-start');
 const before=await profile(),a=await status();
 fs.writeFileSync(out+'/'+label+'-before.json',JSON.stringify(before));
 if (game === 'n-gage') {
  for (let i=0;i<3;++i) { await press(14,1000000); await press(15,1000000); }
  await press(164,1000000); await shot('client-options'); await press(165,1000000);
 }
 await step(seconds*1000000);
 const after=await profile(),b=await status();
 assert.ok(after.rows?.length);assert.equal(errors.length,0);
 assert.equal(b.monitor.instructions,0);
 const report={game,label,edition:entry.version??'launcher default',browser:await browser.version(),
   assets:await page.evaluate(()=>window.ekaAssetUrls),
   cpu:os.cpus()[0].model,requiresSceneReview:true,mode:'instrumented unpaced guest-time route; not an FPS benchmark',
   seconds:(b.ms-a.ms)/1000,guestSeconds:(b.guest-a.guest)/1e6,frames:b.frames-a.frames,errors:[...errors],before:a,after:b};
 fs.writeFileSync(out+'/'+label+'-after.json',JSON.stringify(after));
 fs.writeFileSync(out+'/'+label+'.json',JSON.stringify(report,null,2));
 await shot(label+'-end');
 return report;
};
await page.waitForFunction(()=>Module._eka2l1_route_phase()===1);
const press=async(code,us=1500000)=>{await page.evaluate(c=>Module._eka2l1_key_state(c,1),code);await step(200000);await page.evaluate(c=>Module._eka2l1_key_state(c,0),code);return step(us)};
console.log('READY',game,JSON.stringify(await status()));
 const commands=JSON.parse(fs.readFileSync(new URL('./memory-access-routes.json',import.meta.url)))[game];
 for(const [i,line] of commands.entries()){fs.appendFileSync(out+'/steps.log',line+'\n');const r=await eval(line);console.log('STEP',game,i+1,JSON.stringify(typeof r==='string'?r:{label:r.label,frames:r.frames,guestSeconds:r.guestSeconds,errors:r.errors}));}
fs.writeFileSync(out+'/profile-final.json',JSON.stringify(await profile()));
} catch (error) {fs.writeFileSync(out+'/failure.json',JSON.stringify({error:String(error),errors,status:await status().catch(()=>null)},null,2));throw error;} finally {await browser.close();log.end();}
