import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {spawn,execFileSync} from 'node:child_process';
import {createInterface} from 'node:readline';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const puppeteer=createRequire(new URL('../../wasm/package.json',import.meta.url))('puppeteer');
const [buildArg,outputArg,cpuArg='7',mhzArg='3600',referenceArg='2304']=process.argv.slice(2);
if(!buildArg || !outputArg)throw Error('Usage: node micro.mjs TEST_BUILD NEW_OUTPUT [CPU] [MHZ] [REFERENCE_MHZ]');
const cpuId=Number(cpuArg),targetMhz=Number(mhzArg),referenceMhz=Number(referenceArg);
assert.ok(Number.isInteger(cpuId)&&cpuId>=0&&targetMhz>0&&referenceMhz>0);
const root=path.dirname(fileURLToPath(import.meta.url)),build=path.resolve(buildArg),out=path.resolve(outputArg);fs.mkdirSync(out);
const server=http.createServer((req,res)=>{
 res.setHeader('Cross-Origin-Opener-Policy','same-origin');res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
 if(req.url==='/'){res.setHeader('Content-Type','text/html');res.end('<script>var Module={noInitialRun:true,onRuntimeInitialized(){globalThis.ready=true;},print:console.log,printErr:console.error};</script><script src="/test_aot_wasm.js"></script>');return;}
 const name=path.basename(req.url);
 if(!/^test_aot_wasm\.(js|wasm)$/.test(name)){res.writeHead(404);res.end();return;}
 res.setHeader('Content-Type',name.endsWith('wasm')?'application/wasm':'application/javascript');fs.createReadStream(path.join(build,name)).pipe(res);
});await new Promise(r=>server.listen(0,'127.0.0.1',r));
let browser,counter;
try{
 browser=await puppeteer.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage','--disable-background-timer-throttling',`--js-flags=--perf-prof --perf-prof-path=${out} --logfile=${out}/v8.log`]});
 const system=await browser.target().createCDPSession(),page=await browser.newPage();
 const log=fs.createWriteStream(out+'/browser.log');page.on('console',m=>log.write(m.text()+'\n'));page.on('pageerror',e=>log.write(String(e)+'\n'));
 await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>globalThis.ready,{timeout:120000});
 const before=await system.send('SystemInfo.getProcessInfo');
 for(let i=0;i<16;i++)assert.equal(await page.evaluate(v=>Module._direct_chain_benchmark(v,100000),i%2),0);
 const after=await system.send('SystemInfo.getProcessInfo');
 const renderers=after.processInfo.filter(p=>p.type==='renderer').map(p=>({...p,delta:p.cpuTime-(before.processInfo.find(q=>q.id===p.id)?.cpuTime||0)})).sort((a,b)=>b.delta-a.delta);
 const pid=renderers[0].id;assert.ok(renderers[0].delta>.005);execFileSync('taskset',['-pc',String(cpuId),String(pid)]);
 counter=spawn('python3',[root+'/counters.py'],{stdio:['pipe','pipe','inherit']});
 const lines=createInterface({input:counter.stdout});let waiter;lines.on('line',l=>{assert.ok(waiter);const fn=waiter;waiter=null;fn(JSON.parse(l));});
 const request=r=>new Promise(resolve=>{waiter=resolve;counter.stdin.write(JSON.stringify(r)+'\n');});
 const cpu=()=>Number(fs.readFileSync(`/proc/${pid}/schedstat`,'utf8').split(' ')[0])/1e9;
 const report={browser:await system.send('Browser.getVersion'),pid,renderers,iterations:10000000,rows:[]};
 for(const variant of [0,1,1,0,1,0,0,1]){
  assert.equal(await page.evaluate(v=>Module._direct_chain_benchmark(v,100000),variant),0);
  await request({command:'start',pid});const first=cpu();
  const result=await page.evaluate(({variant,n})=>{const start=performance.now();const status=Module._direct_chain_benchmark(variant,n);return{status,milliseconds:performance.now()-start};},{variant,n:report.iterations});
  const seconds=cpu()-first,hardware=await request({command:'stop'});assert.equal(result.status,0);assert.deepEqual(hardware.errors,[]);
  const h=hardware.events.guest;for(const k of ['user_cycles','user_reference_cycles','user_instructions'])assert.equal(h[k].running_fraction,1);
  const mhz=referenceMhz*h.user_cycles.raw/h.user_reference_cycles.raw;assert.ok(Math.abs(mhz/targetMhz-1)<.005);
  const row={variant,cpu_seconds:seconds,...result,mhz,hardware};report.rows.push(row);fs.writeFileSync(out+'/report.json',JSON.stringify(report,null,2));console.log(JSON.stringify({variant,cpu_seconds:seconds,ms:result.milliseconds,mhz,instructions:h.user_instructions.raw}));
 }
 counter.stdin.end();await browser.close();browser=null;log.end();
}catch(e){console.error(e);process.exitCode=1;}finally{counter?.stdin.end();await browser?.close();server.close();}
