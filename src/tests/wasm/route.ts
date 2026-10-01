// Diagnostic exploration. Paused/unpaced host timings are not performance data.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import readline from 'node:readline';
import puppeteer from 'puppeteer';
import {startServer, buildDir, compilerPolicyFromEnv} from './server.ts';

const [assetArg, outputArg] = process.argv.slice(2);
if (!assetArg || !outputArg) throw Error('Usage: node route.ts ASSETS NEW_OUTPUT');
const assets = path.resolve(assetArg), output = path.resolve(outputArg);
fs.mkdirSync(output);
const compilerPolicy = compilerPolicyFromEnv();
const initialStopUs = Number(process.env.EKA2L1_ROUTE_START_US || '23000000');
if (!Number.isSafeInteger(initialStopUs) || initialStopUs < 2050000 || initialStopUs > 30000000)
  throw Error('Route initial stop must be between 2.05 and 30 guest seconds');
const {server, port} = await startServer(0, {}, undefined, {compilerPolicy});
const browser = await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,
  args:['--no-sandbox','--disable-dev-shm-usage','--use-gl=angle','--use-angle=vulkan',
    '--enable-features=Vulkan','--enable-gpu','--ignore-gpu-blocklist']});
const page = await browser.newPage();
await page.setViewport({width:900,height:760});
const log=fs.createWriteStream(path.join(output,'browser.log'));
const errors:string[]=[];
page.on('console',m=>{log.write(m.text()+'\n');if(m.text().includes('ABORT:'))errors.push(m.text());});
page.on('pageerror',e=>errors.push(String(e)));
page.on('requestfailed',r=>errors.push(`${r.url()}: ${r.failure()?.errorText}`));
page.on('response',r=>{if(r.status()>=400)errors.push(`HTTP ${r.status()} ${r.url()}`);});
const pause=(ms:number)=>new Promise(r=>setTimeout(r,ms));
const call=(name:string,args:number[]=[])=>page.evaluate(({name,args})=>(window as any).Module.ccall(name,'number',args.map(()=> 'number'),args),{name,args});
const state=()=>page.evaluate(()=>{const m=(window as any).Module;return {guest:m._eka2l1_guest_time_us(),inputs:m._eka2l1_input_consumed(),frames:m._eka2l1_presentations(),phase:m._eka2l1_route_phase()};});
async function parked() {
  await page.waitForFunction(()=>(window as any).Module._eka2l1_route_phase()===1,{timeout:120000});
  if(errors.length)throw Error(errors.join('\n'));
  return state();
}
async function stepTo(target:number) {
  if(!Number.isSafeInteger(target)||target<0||target>1800000000)throw Error('Bad target');
  const before=await parked();
  if(target<=before.guest)return before;
  if(await call('eka2l1_route_step_to',[target])!==0)throw Error('Step rejected');
  const after=await parked();
  if(after.guest<target)throw Error('Early pause');
  fs.appendFileSync(path.join(output,'steps.jsonl'),JSON.stringify({target,before,after})+'\n');
  return after;
}
async function stepMs(ms:number) {
  if(!Number.isFinite(ms)||ms<0||ms>30000)throw Error('Bad duration');
  const before=await parked();return stepTo(before.guest+Math.round(ms*1000));
}
async function scan(code:number,down:number) {
  if(!Number.isInteger(code)||code<0||code>255)throw Error('Bad scan');
  const before=await parked();
  const serial=await call('eka2l1_key_state',[code,down]);
  if(serial<0)throw Error('Input rejected');
  fs.appendFileSync(path.join(output,'input.jsonl'),JSON.stringify({guest_us:before.guest,scan:code,down,serial})+'\n');
  return serial;
}
async function key(code:number,hold=70) {
  const serial=await scan(code,1);const after=await stepMs(hold);
  if(after.inputs<serial)throw Error('Input not consumed');
  await scan(code,0);
}
async function visible(name:string) {
  if(!/^[a-zA-Z0-9_-]+$/.test(name))throw Error('Bad screenshot name');
  await parked();await pause(100);
  await (await page.$('#canvas'))!.screenshot({path:path.join(output,name+'.png')});
}
let shutdown=false;
try {
  await page.goto(`http://127.0.0.1:${port}/`,{waitUntil:'domcontentloaded'});
  await page.waitForFunction(()=>(window as any).Module?.calledRun);
  if(await call('eka2l1_route_configure',[-2])!==-1 || await call('eka2l1_route_configure',[0])!==0)
    throw Error('Route configuration contract');
  for(const [selector,file] of [['#rom-file','SYM.ROM'],['#rpkg-file','SYM.RPKG'],['#sis-file','Snakes.sis']])
    await (await page.$(selector))!.uploadFile(path.join(assets,file));
  await page.type('#app-name','Snakes');await page.click('#btn-start');
  await page.waitForFunction(()=>(window as any)._gameRunning,{timeout:120000});
  const initial=await parked();await pause(200);
  if((await state()).guest!==initial.guest)throw Error('Paused guest advanced');
  if(await call('eka2l1_route_configure',[1])!==-1 || await call('eka2l1_route_step_to',[initial.guest])!==-2)
    throw Error('Running/deadline validation contract');
  const appliedPolicy=await page.evaluate(()=>(window as any).ekaCompilerPolicy??null);
  if(compilerPolicy&&(!appliedPolicy?.applied||JSON.stringify(appliedPolicy.requested)!==JSON.stringify(compilerPolicy)))
    throw Error('Wrong compiler policy');
  // Same startup inputs as the live route, now scheduled in guest time.
  for(let us=2000000;us<=20000000 && us+50000<=initialStopUs;us+=2000000){await stepTo(us);await key(167,50);}
  await stepTo(initialStopUs);await visible('initial');
  const ready={diagnostic_only:true,initial_stop_us:initialStopUs,policy:appliedPolicy,state:await state(),
    wasm_sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(buildDir,'eka2l1.wasm'))).digest('hex')};
  fs.writeFileSync(path.join(output,'ready.json'),JSON.stringify(ready,null,2));
  console.log('READY '+JSON.stringify(ready));
  console.log('Commands: {scan,hold,wait_ms,name}, {wait_ms,name}, or {stop:true}. Durations are guest time.');
  const lines=readline.createInterface({input:process.stdin,terminal:false});let count=0;
  for await(const line of lines){
    if(!line.trim())continue;const c=JSON.parse(line);if(c.stop)break;
    if(c.resume||c.pause||c.key)throw Error('Use scan codes; emulator is already parked, no game pause menu');
    if(c.scan!==undefined)await key(c.scan,c.hold??70);
    await stepMs(c.wait_ms??0);
    const name=c.name??`step-${++count}`;await visible(name);
    const result={command:c,state:await state(),screenshot:path.join(output,name+'.png'),errors};
    fs.appendFileSync(path.join(output,'commands.jsonl'),JSON.stringify(result)+'\n');console.log(JSON.stringify(result));
  }
  // Stop while parked: this must wake the emulation thread before joining it.
  await call('eka2l1_shutdown');shutdown=true;
  if(await call('eka2l1_route_phase')!==-1)throw Error('Shutdown phase');
  fs.writeFileSync(path.join(output,'shutdown.json'),JSON.stringify({passed:true,errors}));
} finally {
  await browser.close();server.close();log.end();process.stdin.destroy();
  if(!shutdown)fs.writeFileSync(path.join(output,'incomplete.json'),JSON.stringify({errors}));
}
