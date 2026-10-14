import fs from 'node:fs';
import {PNG} from 'pngjs';
import path from 'node:path';
import os from 'node:os';
import assert from 'node:assert/strict';
import puppeteer from 'puppeteer';
const game=process.argv[2], out=process.argv[3];

const games=JSON.parse(fs.readFileSync(new URL('./games.json',import.meta.url)));
const entry=games.find(g=>g.id===game);
if (!entry || !out || !process.argv[4]) throw Error('Usage: node arena-growth-benchmark.mjs GAME NEW_OUTPUT SERVER_URL');
fs.mkdirSync(out);
const assetDir=process.env.EKA2L1_ASSET_DIR || '/tmp/eka2l1-serve';
const browser=await puppeteer.launch({executablePath:process.env.EKA2L1_CHROMIUM || '/usr/bin/chromium',headless:true,args:['--host-resolver-rules=MAP claude-laptop.lan 127.0.0.1','--no-sandbox','--disable-dev-shm-usage','--use-gl=angle','--use-angle=vulkan','--enable-features=Vulkan','--enable-gpu','--ignore-gpu-blocklist','--disable-background-timer-throttling']});
const system=await browser.target().createCDPSession();
const errors=[],log=fs.createWriteStream(out+'/browser.log');
let status=async()=>null;
try {
const page=await browser.newPage(); await page.setViewport({width:900,height:800});page.setDefaultTimeout(180000);
page.on('console',m=>{log.write(m.text()+'\n');if(/Access violation|KERN-EXEC|ABORT:|panicked with category:/.test(m.text()))errors.push(m.text())});
page.on('pageerror',e=>errors.push(String(e)));
await page.evaluateOnNewDocument(()=>{window.ekaTransientSession=true;});
await page.goto(new URL('?game=custom',process.argv[4]).href,{waitUntil:'domcontentloaded'});
await page.waitForFunction(()=>window.Module?.calledRun);
assert.equal(await page.evaluate(t=>Module._eka2l1_route_configure(t),6000000),0);
for(const [selector,file] of [['#rom-file','SYM.ROM'],['#rpkg-file','SYM.RPKG'],['#sis-file',entry.ngage?.runtime.file ?? entry.file]])await(await page.$(selector)).uploadFile(path.join(assetDir,file));
await page.$eval('#app-name',(e,v)=>e.value=v,entry.uid);
await page.evaluate(entryUrl=>{window.measurementLaunch=startEmulator(entryUrl)},entry.ngage?'/preload/games/'+game+'/ngage':undefined);
await page.waitForFunction(()=>window._gameRunning);
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const shot=async name=>{await page.screenshot({path:out+'/'+name+'.png'});fs.appendFileSync(out+'/route-times.jsonl',JSON.stringify({name,...await status()})+'\n');return out+'/'+name+'.png'};
status=()=>page.evaluate(()=>({ms:performance.now(),guest:Module._eka2l1_guest_time_us(),frames:Module._eka2l1_presentations(),phase:Module._eka2l1_route_phase(),diskScans:window.diskScans?.length||0,inputs:Module._eka2l1_input_consumed(),memory:JSON.parse(Module.ccall('eka2l1_memory_impl_stats','string',[],[])),policy:window.ekaCompilerPolicy,monitor:JSON.parse(Module.ccall('eka2l1_monitor_report','string',[],[]))}));
const step=async(us)=>{const s=await status();assert.equal(s.phase,1);assert.equal(await page.evaluate(t=>Module._eka2l1_route_step_to(t),s.guest+us),0);await page.waitForFunction(()=>Module._eka2l1_route_phase()===1);return status()};
const requireBoatWithoutHelp=name=>{
 const png=PNG.sync.read(fs.readFileSync(out+'/'+name+'.png'));
 let pale=0;for(let y=600;y<750;y++)for(let x=100;x<800;x++){const i=(y*png.width+x)*4;if(png.data[i]>190&&png.data[i+1]>210&&png.data[i+2]>210)pale++;}
 assert(pale<30000,'Hooked On tutorial obscures the measured boat scene');
};
const cpuFrequencies=()=>os.cpus().map((_,i)=>{try{return Number(fs.readFileSync('/sys/devices/system/cpu/cpu'+i+'/cpufreq/scaling_cur_freq','utf8'));}catch{return null;}});
const measure=async(label,seconds=30)=>{
 await shot(label+'-start');
 if(label.startsWith('costa-rica-stationary-tutorial-dismissed'))requireBoatWithoutHelp(label+'-start');
 if(game==='asphalt-3') {
  const png=PNG.sync.read(fs.readFileSync(out+'/'+label+'-start.png'));
  const yellow=(x0,x1)=>{let n=0;for(let y=75;y<160;y++)for(let x=x0;x<x1;x++){const i=(y*png.width+x)*4;if(png.data[i]>180&&png.data[i+1]>100&&png.data[i+2]<80)n++;}return n;};
  const hud={position:yellow(20,175),lap:yellow(770,890)};
  fs.writeFileSync(out+'/race-hud.json',JSON.stringify(hud));
  assert(hud.position>400&&hud.lap>400,'Asphalt 3 is not showing the racing HUD; reject this route');
 }

 const clocksBefore=cpuFrequencies();
 const samples=[await status()],start=performance.now();
 if(game==='n-gage') {await step(seconds*1000000);samples.push(await status());}
 else while(performance.now()-start<seconds*1000){await wait(500);samples.push(await status())}
 const a=samples[0],b=samples.at(-1),wall=(b.ms-a.ms)/1000;
 const report={game,label,hostFrequencyKHz:{before:clocksBefore,after:cpuFrequencies()},edition:entry.version??'launcher default',url:page.url(),browser:await browser.version(),wasm:await page.evaluate(()=>performance.getEntriesByType('resource').find(x=>x.name.includes('.wasm'))?.name),cpu:os.cpus()[0].model,load:os.loadavg(),mode:game==='n-gage'?'bounded guest-time client route; not an FPS benchmark':'unpaced, continuous route with 1800s guest deadline, no pauses',sound:'browser muted; guest audio remains enabled',seconds:wall,frames:b.frames-a.frames,fps:game==='n-gage'?null:(b.frames-a.frames)/wall,guestSeconds:(b.guest-a.guest)/1e6,guestSpeed:(b.guest-a.guest)/1e6/wall,compiledFunctions:b.monitor.compiled_functions-a.monitor.compiled_functions,diskScans:b.diskScans-a.diskScans,compilationUs:Object.fromEntries(['aot_translation_us','aot_emission_us','aot_installation_us'].map(k=>[k,b.monitor[k]-a.monitor[k]])),errors:[...errors],samples};
 assert.ok(samples.every(s=>s.phase===(game==='n-gage'?1:0))); if(game!=='n-gage')assert.ok(report.frames>0);assert.equal(errors.length,0);assert.equal(b.monitor.instructions,0);
 report.browserMemory=[];for(const proc of (await system.send('SystemInfo.getProcessInfo')).processInfo){try{const text=fs.readFileSync(`/proc/${proc.id}/smaps_rollup`,'utf8');report.browserMemory.push({type:proc.type,id:proc.id,pssKiB:Number(text.match(/^Pss:\s+(\d+)/m)?.[1]??0),rssKiB:Number(text.match(/^Rss:\s+(\d+)/m)?.[1]??0)});}catch{}}report.browserPssKiB=report.browserMemory.reduce((n,p)=>n+p.pssKiB,0);report.wasmCapacity=await page.evaluate(()=>HEAPU8.buffer.byteLength);
 fs.writeFileSync(out+'/'+label+'.json',JSON.stringify(report,null,2));await shot(label+'-end');
 if(label.startsWith('costa-rica-stationary-tutorial-dismissed'))requireBoatWithoutHelp(label+'-end');
 return {...report,samples:undefined};
};
await page.waitForFunction(()=>Module._eka2l1_route_phase()===1);
const press=async(code,us=1500000,hold=200000)=>{await page.evaluate(c=>Module._eka2l1_key_state(c,1),code);await step(hold);await page.evaluate(c=>Module._eka2l1_key_state(c,0),code);return step(us)};
const go=async()=>page.evaluate(t=>Module._eka2l1_route_step_to(t),1800000000);
const initialMemory=(await status()).memory;
assert.equal(initialMemory.mode,2);
assert.ok(initialMemory.arena_growths>0);
assert.ok(initialMemory.arena_initialized_bytes<=initialMemory.arena_bytes);
const regionCount=async(name,x0,x1,y0,y1,match)=>{
 await shot(name);const png=PNG.sync.read(fs.readFileSync(out+'/'+name+'.png'));
 let count=0;for(let y=y0;y<y1;y++)for(let x=x0;x<x1;x++){const i=(y*png.width+x)*4;if(match(...png.data.subarray(i,i+3)))count++;}return count;
};
const leaveLanguage=async()=>{
 for(let i=0;i<5;i++){
  await step(10000000);await press(167,5000000,2000000);
  const pink=await regionCount('language-attempt-'+i,200,650,180,700,(r,g,b)=>r>180&&b>140&&g<190);
  if(pink<6000)return shot('after-language-short');
 }
 throw Error('Asphalt 3 language selection did not complete');
};
const pauseFishing=async()=>{
 for(let i=0;i<5;i++){
  await press(164,5000000);
  const pale=await regionCount('pause-attempt-'+i,100,800,330,480,(r,g,b)=>r>190&&g>210&&b>210);
  if(pale>60000)return shot('fishing-pause');
 }
 throw Error('Hooked On fishing pause menu did not open');
};
const ensureRace=async()=>{
 for(let i=0;i<24;i++){
  await step(5000000);await shot('race-ready-'+i);
  assert.equal(errors.length,0,'Reject a crashed Asphalt process');
  const png=PNG.sync.read(fs.readFileSync(out+'/race-ready-'+i+'.png'));
  const count=(x0,x1,y0,y1,test)=>{let n=0;for(let y=y0;y<y1;y++)for(let x=x0;x<x1;x++){const j=(y*png.width+x)*4;if(test(...png.data.subarray(j,j+3)))n++;}return n;};
  const yellow=(r,g,b)=>r>180&&g>100&&b<80;
  if(count(20,175,75,160,yellow)>400&&count(770,890,75,160,yellow)>400)return shot('race-loaded');
  const prompt=count(300,600,370,450,yellow);
  const pink=count(200,750,225,740,(r,g,b)=>r>180&&b>140&&g<190);
  const english=count(220,680,190,260,(r,g,b)=>r<160&&g>180&&b<160);
  if(prompt>1500)await press(164,3000000,1000000);
  else if(english>800)await press(167,8000000,1000000);
  else if(pink>13000)await press(167,8000000,1000000);
  // Loading and splash screens advance without speculative button presses.
 }
 throw Error('Asphalt 3 did not reach its racing HUD');
};
console.log('READY',game,JSON.stringify(await status()));
 const commands=JSON.parse(fs.readFileSync(new URL('./arena-growth-routes.json',import.meta.url)))[game];
 for(const [i,line] of commands.entries()){fs.appendFileSync(out+'/steps.log',line+'\n');const r=await eval(line);console.log('STEP',game,i+1,JSON.stringify(r));}


} catch (error) {fs.writeFileSync(out+'/failure.json',JSON.stringify({error:String(error),errors,status:await status().catch(()=>null)},null,2));throw error;} finally {await browser.close();log.end();}
