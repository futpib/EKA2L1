// Snakes live-menu regression; uses the exact selected application archive.
import puppeteer from 'puppeteer';
import fs from 'node:fs';
import {execFileSync} from 'node:child_process';
import {startServer,compilerPolicyFromEnv} from './server.ts';
const [assets,out,url]=process.argv.slice(2);
if(!assets||!out)throw Error('Usage: node softkeys.ts ASSETS NEW_OUTPUT [EXISTING_URL]; requires ImageMagick and Tesseract');
const expectedPolicy={irMode:7,eagerRegions:0,...compilerPolicyFromEnv()};
const local=!url ? await startServer(0,{'/preload/rom':assets+'/SYM.ROM','/preload/rpkg':assets+'/SYM.RPKG','/preload/sis':assets+'/Snakes.sis'},'Snakes',{compilerPolicy:expectedPolicy}) : null;
const target=local?`http://127.0.0.1:${local.port}/`:url;
import png from 'pngjs';
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,ignoreDefaultArgs:['--mute-audio'],args:['--no-sandbox','--disable-dev-shm-usage','--use-gl=angle','--use-angle=vulkan','--enable-features=Vulkan','--enable-gpu','--ignore-gpu-blocklist']});
const page=await browser.newPage(), errors=[];fs.mkdirSync(out);
page.on('pageerror',e=>errors.push(String(e)));page.on('requestfailed',r=>errors.push(r.url()));page.on('response',r=>{if(r.status()>=400)errors.push(r.url()+': '+r.status());});
try {
 await page.setViewport({width:900,height:760,hasTouch:true});await page.goto(target,{waitUntil:'domcontentloaded'});
 await page.waitForFunction(()=>window._gameRunning,{timeout:180000});
 const compilerPolicy=await page.evaluate(()=>window.ekaCompilerPolicy);if(!compilerPolicy?.applied || JSON.stringify(compilerPolicy.requested)!==JSON.stringify(expectedPolicy))throw Error('Wrong served compiler policy');
 const security=await page.evaluate(()=>({secure:isSecureContext,isolated:crossOriginIsolated}));if(!security.secure||!security.isolated)throw Error('Context isolation');
 for(let t=2000000;t<=20000000;t+=2000000){await page.waitForFunction(t=>Module._eka2l1_guest_time_us()>=t,{timeout:120000},t);await page.keyboard.press('Enter',{delay:50});}
 await page.waitForFunction(()=>Module._eka2l1_guest_time_us()>=23000000,{timeout:120000});
 if(!await page.evaluate(()=>EkaAudio.context===null && EkaAudio.muted))throw Error('Unexpected autoplay');
 await page.click('#btn-sound');
 await page.waitForFunction(()=>EkaAudio.context?.state==='running' && !EkaAudio.muted);
 if(!await page.evaluate(()=>document.activeElement.id==='canvas'))throw Error('Sound button trapped keyboard focus');
 await page.evaluate(()=>{window.audioAnalyser=EkaAudio.context.createAnalyser();audioAnalyser.fftSize=2048;EkaAudio.gain.connect(audioAnalyser);});
 const amplitude=()=>page.evaluate(()=>{const x=new Float32Array(audioAnalyser.fftSize);audioAnalyser.getFloatTimeDomainData(x);return Math.max(...x.map(Math.abs));});
 await new Promise(r=>setTimeout(r,500));if(await amplitude()===0)throw Error('Silent sound output');
 await page.click('#btn-sound');await new Promise(r=>setTimeout(r,150));if(await amplitude()!==0)throw Error('Mute output nonzero');
 await page.click('#btn-sound');await new Promise(r=>setTimeout(r,150));if(await amplitude()===0)throw Error('Unmute output silent');
 const state=()=>page.evaluate(()=>({guest:Module._eka2l1_guest_time_us(),inputs:Module._eka2l1_input_consumed(),frames:Module._eka2l1_presentations()}));
 const first=await state();await page.keyboard.press('ArrowRight',{delay:100});await new Promise(r=>setTimeout(r,500));const second=await state();if(second.inputs<=first.inputs)throw Error('Keyboard');
 await (await page.$('#canvas')).screenshot({path:out+'/gameplay.png'});await page.setViewport({width:390,height:844,hasTouch:true});await page.screenshot({path:out+'/mobile.png'});
 if(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth))throw Error('Overflow');
 await (await page.$('[data-scan="14"]')).tap();await new Promise(r=>setTimeout(r,200));const last=await state();if(last.inputs<second.inputs+2)throw Error('Touch');
 const softkeys=[];
 const scene=async(name,paused)=>{
  await new Promise(r=>setTimeout(r,500));
  const file=out+'/'+name+'.png';await(await page.$('#canvas')).screenshot({path:file});
  const crop=execFileSync('magick',[file,'-crop','80x18+80+105','-resize','400%','png:-']);
  const text=execFileSync('tesseract',['stdin','stdout','--psm','7'],{input:crop,encoding:'utf8',stdio:['pipe','pipe','pipe']});
  const seen=/Paused/i.test(text);softkeys.push({name,expected_paused:paused,seen,text,state:await state()});
  if(seen!==paused)throw Error('Pause menu mismatch '+name+': '+text);
 };
 await page.keyboard.press('F1',{delay:70});await scene('f1-paused',true);
 await page.keyboard.press('F2',{delay:70});await scene('f2-resumed',false);
 await page.keyboard.press('Escape',{delay:70});await scene('escape-paused',true);
 await(await page.$('[data-scan="164"]')).tap();await scene('touch-left-resumed',false);
 await(await page.$('[data-scan="165"]')).tap();await scene('touch-right-paused',true);
 await page.keyboard.press('Escape',{delay:70});await scene('escape-resumed',false);
 const canvasSamples=[];for(let i=0;i<12;i++){const bytes=await(await page.$('#canvas')).screenshot();const pixels=png.PNG.sync.read(Buffer.from(bytes)).data;const colors=new Set();for(let j=0;j<pixels.length;j+=4)colors.add((pixels[j]<<16)|(pixels[j+1]<<8)|pixels[j+2]);canvasSamples.push(colors.size);await new Promise(r=>setTimeout(r,250));}
 const audio=await page.evaluate(()=>({state:EkaAudio.context.state,stats:EkaAudio.stats,rate:EkaAudio.context.sampleRate}));if(!audio.stats.nonzero || audio.stats.dropped || audio.stats.underruns)throw Error('LAN audio continuity');
 await page.evaluate(()=>{window._gameRunning=false;Module._eka2l1_shutdown();});if(errors.length)throw Error(errors.join('\n'));
 fs.writeFileSync(out+'/report.json',JSON.stringify({target,softkeys,compilerPolicy,security,audio,first,second,last,canvasSamples,errors,browser:await browser.version()},null,2));console.log('PASS softkeys open/resume the game menu; gesture audio, mute/unmute, keyboard/touch, layout and shutdown');
} finally {await browser.close();local?.server.close();}
