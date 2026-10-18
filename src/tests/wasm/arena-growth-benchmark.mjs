import fs from 'node:fs';
import {configureRomCacheCapture,captureRomCache} from './rom-cache-capture.mjs';
import {PNG} from 'pngjs';
import path from 'node:path';
import os from 'node:os';
import assert from 'node:assert/strict';
import puppeteer from 'puppeteer';
const game=process.argv[2], out=process.argv[3];
const fullMemory=process.argv[5]==='full-memory';
const persistentDisk=process.env.EKA2L1_BENCHMARK_PERSISTENT_DISK==='1';
const freshLauncher=process.env.EKA2L1_BENCHMARK_FRESH_LAUNCHER==='1';
if(freshLauncher&&!persistentDisk)throw Error('Fresh launcher requires persistent disk');

const games=JSON.parse(fs.readFileSync(new URL('./games.json',import.meta.url)));
const entry=games.find(g=>g.id===game);
if (!entry || !out || !process.argv[4]) throw Error('Usage: node arena-growth-benchmark.mjs GAME NEW_OUTPUT SERVER_URL');
if(freshLauncher&&!entry.ngage)throw Error('Fresh launcher route requires an N-Gage game');
fs.mkdirSync(out);
const assetDir=process.env.EKA2L1_ASSET_DIR || '/tmp/eka2l1-serve';
const browserArgs=['--host-resolver-rules=MAP claude-laptop.lan 127.0.0.1','--no-sandbox','--disable-dev-shm-usage','--use-gl=angle','--use-angle=vulkan','--enable-features=Vulkan','--enable-gpu','--ignore-gpu-blocklist','--disable-background-timer-throttling'];
if(process.env.EKA2L1_RENDERER_CGROUP_PREFIX)browserArgs.push('--renderer-cmd-prefix='+process.env.EKA2L1_RENDERER_CGROUP_PREFIX);
if(fullMemory)browserArgs.push('--js-flags=--perf-basic-prof');
const browser=await puppeteer.launch({executablePath:process.env.EKA2L1_CHROMIUM || '/usr/bin/chromium',headless:true,args:browserArgs});
const system=await browser.target().createCDPSession();
const errors=[],log=fs.createWriteStream(out+'/browser.log');
let status=async()=>null;
let memoryProfiler;
const allocationProfile = process.argv[5] === 'allocator-memory';
const allocationSnapshots=[];
const runtimeLayout=process.env.EKA2L1_ALLOCATION_LAYOUT ? JSON.parse(fs.readFileSync(process.env.EKA2L1_ALLOCATION_LAYOUT)) : undefined;
const browserMemorySamples=[];
const browserMemory=async()=>{
 const rows=[];
 for(const proc of (await system.send('SystemInfo.getProcessInfo')).processInfo){
  try{const text=fs.readFileSync(`/proc/${proc.id}/smaps_rollup`,'utf8');rows.push({type:proc.type,id:proc.id,cpuSeconds:proc.cpuTime,pssKiB:Number(text.match(/^Pss:\s+(\d+)/m)?.[1]??0),rssKiB:Number(text.match(/^Rss:\s+(\d+)/m)?.[1]??0)});}catch{}
 }
 return rows;
};
let browserMemoryTimer;
const profileAfterRuntime=process.env.EKA2L1_CHROME_PROFILE_AFTER_RUNTIME==='1';
try {
const page=process.env.EKA2L1_RENDERER_CGROUP_PREFIX?(await browser.pages())[0]??await browser.newPage():await browser.newPage(); await page.setViewport({width:900,height:800});page.setDefaultTimeout(180000);
let mainNavigations=0;
page.on('framenavigated',frame=>{if(frame===page.mainFrame()){
 mainNavigations++;
 fs.appendFileSync(out+'/navigations.jsonl',JSON.stringify({ms:Date.now(),count:mainNavigations,url:frame.url()})+'\n');
}});
if(process.argv[5]==='chrome-memory' || allocationProfile || fullMemory) {
 const {ChromeMemoryProfiler}=await import('./chrome-memory-profiler.mjs');
 memoryProfiler=new ChromeMemoryProfiler(page,out);
 if(!profileAfterRuntime)await memoryProfiler.start();
}
page.on('console',m=>{log.write(m.text()+'\n');if(/Access violation|KERN-EXEC|ABORT:|panicked with category:|AOT-only:|RuntimeError:|Aborted\(|Thread Main forcefully killed.*exit code: -/.test(m.text()))errors.push(m.text())});
page.on('pageerror',e=>errors.push(String(e)));
const guardedWait=async predicate=>{
 const abort=new AbortController();
 const timer=setInterval(()=>{if(errors.length)abort.abort(new Error(errors.join('\n')));},250);
 try{return await page.waitForFunction(predicate,{signal:abort.signal});}finally{clearInterval(timer);}
};
await page.evaluateOnNewDocument((transient,fresh)=>{
 window.ekaTransientSession=transient;
 window.ekaForceFreshInstall=fresh;
 if(fresh)window.ekaBenchmarkRouteUs=6000000;
},!persistentDisk,freshLauncher);
await page.goto(new URL('?game='+(freshLauncher?game:'custom'),process.argv[4]).href,{waitUntil:'domcontentloaded'});
if(allocationProfile || fullMemory || process.env.EKA2L1_BROWSER_MEMORY==='1')browserMemoryTimer=setInterval(async()=>{
 try{const processes=await browserMemory();browserMemorySamples.push({ms:performance.now(),processes,pssKiB:processes.reduce((n,p)=>n+p.pssKiB,0)});}catch{}
},1000);
await guardedWait(()=>window.Module?.calledRun);
if(memoryProfiler && profileAfterRuntime)await memoryProfiler.start();
if(!freshLauncher)assert.equal(await page.evaluate(t=>Module._eka2l1_route_configure(t),6000000),0);
let installedImage;
const html=await page.evaluate(async()=>await(await fetch('/')).text());
const launch=html.match(/<script>configureGameLauncher\((\[.*\]), [^;]+\);<\/script>/);
assert(launch,'Missing launcher configuration');
const launchGame=JSON.parse(launch[1]).find(g=>g.id===game);
if(process.env.EKA2L1_ROM_CACHE_CAPTURE==='1')await configureRomCacheCapture(page);
else if(launchGame?.romCache)await page.evaluate(cache=>EkaRomCache.prepare(cache),launchGame.romCache);
if(freshLauncher) {
 assert.equal(process.env.EKA2L1_BENCHMARK_PREPARED,undefined);
}else if(process.env.EKA2L1_BENCHMARK_PREPARED==='1') {
 installedImage=launchGame?.image;
 assert(installedImage,'This game has no prepared installation on this server');
}else for(const [selector,file] of [['#rom-file','SYM.ROM'],['#rpkg-file','SYM.RPKG'],['#sis-file',entry.ngage?.runtime.file ?? entry.file]])await(await page.$(selector)).uploadFile(path.join(assetDir,file));
if(!freshLauncher){
 await page.$eval('#app-name',(e,v)=>e.value=v,entry.uid);
 await page.evaluate(({entryUrl,image})=>{window.measurementLaunch=startEmulator(entryUrl,image)},{entryUrl:!installedImage&&entry.ngage?'/preload/games/'+game+'/ngage':undefined,image:installedImage});
 await guardedWait(()=>window._gameRunning);
}else{
 const deadline=Date.now()+600000;
 let nextProgress=0;
 while(Date.now()<deadline){
  if(errors.length)throw Error(errors.join('\n'));
  if(Date.now()>=nextProgress){
   fs.appendFileSync(out+'/fresh-launch-progress.jsonl',JSON.stringify({ms:Date.now(),mainNavigations,url:page.url()})+'\n');
   nextProgress=Date.now()+5000;
  }
  try{if(mainNavigations>=5&&await page.evaluate(()=>Module.calledRun&&window._gameRunning&&window.ekaDeviceDisk?.manifest))break;}catch{}
  await new Promise(resolve=>setTimeout(resolve,250));
 }
 assert(Date.now()<deadline,'Fresh launcher did not reach the running game');
}
if(persistentDisk)assert.equal(await page.evaluate(()=>!!window.ekaDeviceDisk),true);
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const shot=async name=>{
 await page.screenshot({path:out+'/'+name+'.png'});
 const current=await status();
 fs.appendFileSync(out+'/route-times.jsonl',JSON.stringify({name,...current})+'\n');
 if(process.env.EKA2L1_BROWSER_MEMORY_PHASES==='1') {
  const processes=await browserMemory();
  fs.writeFileSync(out+'/'+name+'.browser-memory.json',JSON.stringify(processes));
  for(const process of processes)if(process.type==='renderer') {
   try{fs.writeFileSync(out+'/'+name+'-'+process.id+'.smaps',fs.readFileSync(`/proc/${process.id}/smaps`));}catch{}
  }
 }
 if(allocationProfile){
  const profile=await page.evaluate(()=>JSON.parse(Module.ccall('eka2l1_allocation_profile_report','string',[],[])));
  const processes=await browserMemory();
  const snapshot={name,...current,browserMemory:processes,browserPssKiB:processes.reduce((n,p)=>n+p.pssKiB,0),wasmCapacity:await page.evaluate(()=>HEAPU8.buffer.byteLength),allocation:profile};
  if(runtimeLayout){
   assert(runtimeLayout.decoded_used_offset+4<=runtimeLayout.state_size);
   const candidates=profile.large_allocations.filter(a=>a.requested===runtimeLayout.state_size);
   assert.equal(candidates.length,1,'The measured interpreter state must be uniquely identified by its target ABI size');
   snapshot.interpreter={...runtimeLayout,pointer:candidates[0].pointer,decoded_used:await page.evaluate(({pointer,offset})=>HEAPU32[(pointer+offset)>>>2],{pointer:candidates[0].pointer,offset:runtimeLayout.decoded_used_offset})};
   assert(snapshot.interpreter.decoded_used<=runtimeLayout.decoded_capacity);
  }
  if((name.endsWith('-end') || name===process.env.EKA2L1_ARENA_RESIDENCY_SHOT) && profile.large_allocations){
   const {allocationResidency}=await import('./allocation-residency.mjs');
   try{snapshot.residency=await allocationResidency(page,processes,snapshot.wasmCapacity,profile.large_allocations);}catch(error){snapshot.residency={error:String(error)};}
   if(await page.evaluate(()=>typeof Module._eka2l1_memory_access_profile_report==='function')){
    const guestMemory=await page.evaluate(()=>JSON.parse(Module.ccall('eka2l1_memory_access_profile_report','string',[],[])));
    fs.writeFileSync(out+'/'+name+'.guest-memory.json',JSON.stringify(guestMemory));
   }
  }
  fs.writeFileSync(out+'/'+name+'.allocations.json',JSON.stringify(snapshot,null,2));
  allocationSnapshots.push({name,live_bytes:profile.live_bytes,peak_bytes:profile.peak_bytes,mallinfo_allocated_bytes:profile.mallinfo_allocated_bytes,dropped:profile.dropped,unknown_frees:profile.unknown_frees});
  if(name.endsWith('-end'))for(const proc of processes){try{fs.writeFileSync(out+'/'+name+'-'+proc.type.replace(/[^\w-]/g,'_')+'-'+proc.id+'.smaps',fs.readFileSync(`/proc/${proc.id}/smaps`));}catch{}}
 }
 return out+'/'+name+'.png';
};
status=()=>page.evaluate(()=>({ms:performance.now(),guest:Module._eka2l1_guest_time_us(),frames:Module._eka2l1_presentations(),phase:Module._eka2l1_route_phase(),wasmCapacity:HEAPU8.buffer.byteLength,diskScans:window.diskScans?.length||0,inputs:Module._eka2l1_input_consumed(),memory:JSON.parse(Module.ccall('eka2l1_memory_impl_stats','string',[],[])),policy:window.ekaCompilerPolicy,workerPool:typeof PThread==='undefined'?null:{running:PThread.runningWorkers.length,unused:PThread.unusedWorkers.length},monitor:JSON.parse(Module.ccall('eka2l1_monitor_report','string',[],[]))}));
const step=async(us)=>{const s=await status();assert.equal(s.phase,1);assert.equal(await page.evaluate(t=>Module._eka2l1_route_step_to(t),s.guest+us),0);await guardedWait(()=>Module._eka2l1_route_phase()===1);return status()};
const requireBoatWithoutHelp=name=>{
 const png=PNG.sync.read(fs.readFileSync(out+'/'+name+'.png'));
 let pale=0;for(let y=600;y<750;y++)for(let x=100;x<800;x++){const i=(y*png.width+x)*4;if(png.data[i]>190&&png.data[i+1]>210&&png.data[i+2]>210)pale++;}
 assert(pale<30000,'Hooked On tutorial obscures the measured boat scene');
};
const skyHud=png=>{
 const count=(x0,x1,y0,y1,test)=>{let n=0;for(let y=y0;y<y1;y++)for(let x=x0;x<x1;x++){const i=(y*png.width+x)*4;if(test(...png.data.subarray(i,i+3)))n++;}return n;};
 const black=(r,g,b)=>r<80&&g<80&&b<80;
 const hud={health:count(185,310,43,57,(r,g,b)=>r>140&&g>35&&g<230&&b<90&&r>=g),score:count(350,550,0,36,black),percent:count(705,739,0,40,black),life:count(151,177,0,45,black)};
 return {...hud,valid:hud.health>400&&hud.score>1000&&hud.percent>400&&hud.life>200};
};
const cpuFrequencies=()=>os.cpus().map((_,i)=>{try{return Number(fs.readFileSync('/sys/devices/system/cpu/cpu'+i+'/cpufreq/scaling_cur_freq','utf8'));}catch{return null;}});
const measure=async(label,seconds=30)=>{
 await shot(label+'-start');
 if(game==='sky-force')assert(skyHud(PNG.sync.read(fs.readFileSync(out+'/'+label+'-start.png'))).valid,'Sky Force must show health, score, percent and life HUD; reject logos and menus');
 if(label.startsWith('costa-rica-stationary-tutorial-dismissed'))requireBoatWithoutHelp(label+'-start');
 if(game==='n-gage') {
  const png=PNG.sync.read(fs.readFileSync(out+'/'+label+'-start.png'));
  let mint=0;for(let y=450;y<700;y++)for(let x=180;x<720;x++){const i=(y*png.width+x)*4;if(png.data[i]>100&&png.data[i+1]>170&&png.data[i+1]>png.data[i]+10&&png.data[i+2]>png.data[i]+10)mint++;}
  assert(mint>60000,'N-Gage must show its client UI; reject black screens and firmware wallpaper');
 }
 if(game==='super-miners') {
  const png=PNG.sync.read(fs.readFileSync(out+'/'+label+'-start.png'));
  let score=0,hearts=0;
  for(let y=0;y<80;y++)for(let x=150;x<550;x++){const i=(y*png.width+x)*4;if(y<50&&x>400&&png.data[i]>210&&png.data[i+1]>210&&png.data[i+2]>210)score++;if(x<330&&png.data[i]>170&&png.data[i+1]<110&&png.data[i+2]<110)hearts++;}
  assert(score>1000&&hearts>250,'Super Miners must show score and heart HUD; reject loading screens and menus');
 }
 if(game==='asphalt-2') {
  const png=PNG.sync.read(fs.readFileSync(out+'/'+label+'-start.png'));
  const cyan=(x0,x1,y0,y1)=>{let n=0;for(let y=y0;y<y1;y++)for(let x=x0;x<x1;x++){const i=(y*png.width+x)*4;if(png.data[i+1]>150&&png.data[i+2]>150&&png.data[i]<png.data[i+1]-20)n++;}return n;};
  assert(cyan(580,745,60,95)>2000&&cyan(160,210,50,95)>500,'Asphalt 2 must show position and lap HUD; reject loading screens and menus');
 }

 if(game==='asphalt-3') {
  const png=PNG.sync.read(fs.readFileSync(out+'/'+label+'-start.png'));
  const yellow=(x0,x1)=>{let n=0;for(let y=75;y<160;y++)for(let x=x0;x<x1;x++){const i=(y*png.width+x)*4;if(png.data[i]>180&&png.data[i+1]>100&&png.data[i+2]<80)n++;}return n;};
  const hud={position:yellow(20,175),lap:yellow(770,890)};
  fs.writeFileSync(out+'/race-hud.json',JSON.stringify(hud));
  assert(hud.position>400&&hud.lap>400,'Asphalt 3 is not showing the racing HUD; reject this route');
 }

 const clocksBefore=cpuFrequencies();
 const processesBefore=await browserMemory();
 const samples=[await status()],start=performance.now();
 if(game==='n-gage') {await step(seconds*1000000);samples.push(await status());}
 else while(performance.now()-start<seconds*1000){await wait(500);samples.push(await status())}
 const a=samples[0],b=samples.at(-1),wall=(b.ms-a.ms)/1000;
 const report={game,label,hostFrequencyKHz:{before:clocksBefore,after:cpuFrequencies()},edition:entry.version??'launcher default',url:page.url(),browser:await browser.version(),wasm:await page.evaluate(()=>performance.getEntriesByType('resource').find(x=>x.name.includes('.wasm'))?.name),cpu:os.cpus()[0].model,load:os.loadavg(),mode:game==='n-gage'?'bounded guest-time client route; not an FPS benchmark':'unpaced, continuous route with 1800s guest deadline, no pauses',sound:'browser muted; guest audio remains enabled',seconds:wall,frames:b.frames-a.frames,fps:game==='n-gage'?null:(b.frames-a.frames)/wall,guestSeconds:(b.guest-a.guest)/1e6,guestSpeed:(b.guest-a.guest)/1e6/wall,compiledFunctions:b.monitor.compiled_functions-a.monitor.compiled_functions,diskScans:b.diskScans-a.diskScans,compilationUs:Object.fromEntries(['aot_translation_us','aot_emission_us','aot_installation_us'].map(k=>[k,b.monitor[k]-a.monitor[k]])),errors:[...errors],samples};
 report.persistentDisk=persistentDisk;
 report.firmwareOffload=await page.evaluate(()=>{
  const disk=window.ekaDeviceDisk;
  if(!disk)return {done:false,files:0};
  let files=0;
  const stack=['/data/drives/z'];
  while(stack.length){
   const path=stack.pop();
   for(const name of FS.readdir(path))if(name!=='.'&&name!=='..'){
    const child=path+'/'+name,kind=FS.lstat(child).mode&0xf000;
    if(kind===0x4000)stack.push(child);
    else if(kind===0xa000)files++;
   }
  }
  return {done:!!disk.firmwareOffloadDone,files};
 });
 if(process.env.EKA2L1_MEMORY_FILE_INVENTORY==='1')report.fileInventory=await page.evaluate(()=>{
  const files=[],errors=[];
  const visit=(path)=>{
   const info=FS.lstat(path);
   if((info.mode&0xf000)===0xa000)return;
   if((info.mode&0xf000)===0x4000){
    for(const name of FS.readdir(path))if(name!=='.'&&name!=='..')visit(path+'/'+name);
   }else files.push({path,size:info.size});
  };
  for(const root of ['/data','/tmp'])try{visit(root);}catch(error){errors.push({path:root,error:String(error)});}
  return {count:files.length,bytes:files.reduce((sum,file)=>sum+file.size,0),
   top:files.sort((a,b)=>b.size-a.size).slice(0,60),errors};
 });
 report.romCache=await page.evaluate(()=>({staging:window.ekaRomCacheStatus??null,status:Module._eka2l1_rom_cache_status?.()??null}));
 report.chromeMemoryProfiling=!!memoryProfiler;
 report.allocationProfiling=allocationProfile;
 report.preparedInstallation=!!installedImage;
 assert.ok(samples.every(s=>s.phase===(game==='n-gage'?1:0))); if(game!=='n-gage')assert.ok(report.frames>0);assert.equal(errors.length,0);assert.equal(b.monitor.instructions,0);
 report.browserMemory=await browserMemory();
 if(fullMemory || process.env.EKA2L1_BROWSER_MEMORY==='1')for(const p of report.browserMemory){try{fs.writeFileSync(out+'/'+label+'-'+p.id+'.smaps',fs.readFileSync(`/proc/${p.id}/smaps`));}catch{}}
 report.browserCpuSeconds=report.browserMemory.reduce((n,p)=>n+p.cpuSeconds-(processesBefore.find(b=>b.id===p.id)?.cpuSeconds??0),0);
 report.browserCpuMsPerFrame=report.frames?report.browserCpuSeconds*1000/report.frames:null;
 report.browserPssKiB=report.browserMemory.reduce((n,p)=>n+p.pssKiB,0);report.wasmCapacity=await page.evaluate(()=>HEAPU8.buffer.byteLength);
 report.observedBrowserPeakPssKiB=Math.max(report.browserPssKiB,...browserMemorySamples.map(s=>s.pssKiB));
 fs.writeFileSync(out+'/'+label+'.json',JSON.stringify(report,null,2));await shot(label+'-end');
 if(fullMemory){
  for(const process of report.browserMemory){
   const source=`/tmp/perf-${process.id}.map`;
   if(fs.existsSync(source))fs.copyFileSync(source,path.join(out,`${label}-${process.id}.perf.map`));
  }
  const renderer=report.browserMemory.filter(p=>p.type==='renderer').sort((a,b)=>b.pssKiB-a.pssKiB)[0];
  const perfMap=path.join(out,`${label}-${renderer.id}.perf.map`);
  const smaps=path.join(out,`${label}-${renderer.id}.smaps`);
  if(fs.existsSync(perfMap)&&fs.existsSync(smaps)){
   const {jitCodeResidency}=await import('./jit-code-residency.mjs');
   fs.writeFileSync(path.join(out,`${label}.jit-code-residency.json`),JSON.stringify(jitCodeResidency(renderer.id,perfMap,smaps),null,2));
  }
 }
 if(label.startsWith('costa-rica-stationary-tutorial-dismissed'))requireBoatWithoutHelp(label+'-end');
 return {...report,samples:undefined};
};
await guardedWait(()=>Module._eka2l1_route_phase()===1);
const press=async(code,us=1500000,hold=200000)=>{await page.evaluate(c=>Module._eka2l1_key_state(c,1),code);await step(hold);await page.evaluate(c=>Module._eka2l1_key_state(c,0),code);return step(us)};
const go=async()=>page.evaluate(t=>Module._eka2l1_route_step_to(t),1800000000);
const initialMemory=(await status()).memory;
if(allocationProfile)assert.equal(await page.evaluate(()=>typeof Module._eka2l1_allocation_profile_report),'function');
assert.equal(initialMemory.mode,2);
assert.ok(initialMemory.arena_growths>0);
assert.ok(initialMemory.arena_initialized_bytes<=initialMemory.arena_bytes);
const regionCount=async(name,x0,x1,y0,y1,match)=>{
 await shot(name);const png=PNG.sync.read(fs.readFileSync(out+'/'+name+'.png'));
 let count=0;for(let y=y0;y<y1;y++)for(let x=x0;x<x1;x++){const i=(y*png.width+x)*4;if(match(...png.data.subarray(i,i+3)))count++;}return count;
};
const ensureGameplay=async()=>{
 for(let i=0;i<16;i++) {
  const white=await regionCount('gameplay-ready-'+i,game==='snakes'?330:150,game==='snakes'?630:750,0,game==='snakes'?45:90,(r,g,b)=>r>200&&g>200&&b>200);
  if(game==='snakes') {
   const blue=await regionCount('gameplay-blue-'+i,330,630,43,56,(r,g,b)=>b>160&&r<100);
   if(white>200&&blue>50)return shot('gameplay-ready');
  } else if(skyHud(PNG.sync.read(fs.readFileSync(out+'/gameplay-ready-'+i+'.png'))).valid)return shot('gameplay-ready');
  await press(167,10000000,200000);
 }
 throw Error(game+' did not reach its gameplay HUD');
};
const awaitNgageClient=async()=>{
 for(let i=0;i<24;i++) {
  const mint=await regionCount('client-ready-'+i,180,720,450,700,(r,g,b)=>r>100&&g>170&&g>r+10&&b>r+10);
  if(mint>60000)return shot('client-ready');
  await step(10000000);
 }
 throw Error('N-Gage did not reach its client UI');
};
const ensureMiners=async()=>{
 for(let i=0;i<20;i++) {
  const white=await regionCount('miners-ready-'+i,400,550,0,50,(r,g,b)=>r>210&&g>210&&b>210);
  const hearts=await regionCount('miners-hearts-'+i,150,330,0,80,(r,g,b)=>r>170&&g<110&&b<110);
  if(white>1000&&hearts>250)return shot('miners-ready');
  await press(167,10000000);
 }
 throw Error('Super Miners did not reach its gameplay HUD');
};
const awaitAsphaltSound=async()=>{
 for(let i=0;i<50;i++) {
  const white=await regionCount('sound-ready-'+i,330,570,385,407,(r,g,b)=>r>210&&g>210&&b>210);
  if(white>1700)return shot('sound-ready');
  await step(5000000);
 }
 throw Error('Asphalt 2 did not reach its sound prompt');
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
  const instantPlay=count(180,740,255,320,(r,g,b)=>r<160&&g>180&&b<160);
  if(prompt>1500)await press(164,3000000,1000000);
  else if(english>800)await press(167,8000000,1000000);
  else if(instantPlay>2000)await press(167,10000000,1000000);
  else if(pink>13000)await press(167,8000000,1000000);
  // Loading and splash screens advance without speculative button presses.
 }
 throw Error('Asphalt 3 did not reach its racing HUD');
};
console.log('READY',game,JSON.stringify(await status()));
 const commands=JSON.parse(fs.readFileSync(process.env.EKA2L1_BENCHMARK_ROUTES || new URL('./arena-growth-routes.json',import.meta.url)))[game];
 for(const [i,line] of commands.entries()){fs.appendFileSync(out+'/steps.log',line+'\n');const r=await eval(line);console.log('STEP',game,i+1,JSON.stringify(r));}
 if(process.env.EKA2L1_ROM_CACHE_CAPTURE==='1') {
  clearInterval(browserMemoryTimer);browserMemoryTimer=undefined;
  await new Promise(resolve=>setTimeout(resolve,100));
  console.log('ROM CACHE',JSON.stringify(await captureRomCache(page,out,game)));
 }



} catch (error) {fs.writeFileSync(out+'/failure.json',JSON.stringify({error:String(error),errors,status:await status().catch(()=>null)},null,2));throw error;} finally {clearInterval(browserMemoryTimer);try{if(memoryProfiler)await memoryProfiler.stop();if(allocationProfile)fs.writeFileSync(out+'/allocation-stages.json',JSON.stringify(allocationSnapshots,null,2));if(browserMemorySamples.length)fs.writeFileSync(out+'/browser-memory-samples.json',JSON.stringify(browserMemorySamples,null,2));}finally{await browser.close();log.end();}}
