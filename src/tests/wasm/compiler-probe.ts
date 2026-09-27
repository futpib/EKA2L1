// Offline, ordinary-memory kernel experiment. Never launches or changes the game.
import fs from 'node:fs';
import path from 'node:path';
import puppeteer from 'puppeteer';
import http from 'node:http';
const [directory, output] = process.argv.slice(2);
if (!directory || !output) throw Error('compiler-probe.ts KERNEL_DIRECTORY NEW_OUTPUT');
fs.mkdirSync(output);
const html=path.join(output,'probe.html');fs.writeFileSync(html,'<!doctype html><title>Compiler experiment</title>');
const files:Record<string,string>={'/probe.html':path.resolve(html)};
for(const f of fs.readdirSync(directory)) if(f.endsWith('.wasm'))files['/'+f]=path.resolve(directory,f);
const server=http.createServer((req,res)=>{const file=files[req.url||''];if(!file){res.writeHead(404);res.end();return;}res.writeHead(200,{'Content-Type':req.url?.endsWith('.html')?'text/html':'application/wasm','Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp'});fs.createReadStream(file).pipe(res);});
await new Promise<void>(r=>server.listen(0,'127.0.0.1',r));const port=(server.address() as any).port;
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,dumpio:process.env.EKA2L1_V8_DUMP === '1',args:['--no-sandbox',...(process.env.EKA2L1_V8_FLAGS?[`--js-flags=${process.env.EKA2L1_V8_FLAGS}`]:[])],protocolTimeout:600000});
try {
 const page=await browser.newPage();await page.goto(`http://127.0.0.1:${port}/probe.html`);
 const result=await page.evaluate(async(validateOnly)=>{
  const all:any[]=[];
  for(const [pc,cycles] of [[1879455500,57],[1879129820,7]]) {
   const memory=new WebAssembly.Memory({initial:256,maximum:32768,shared:true});const u=new Uint32Array(memory.buffer);const bytes=new Uint8Array(memory.buffer);const s=1024;
   const variants:Record<string,any>={};const compilation:Record<string,number>={};
   for(const suffix of ['','opt','ir','ir-opt','whole']) {
    const b=await (await fetch('/'+pc+(suffix?'.'+suffix:'')+'.wasm')).arrayBuffer();const begin=performance.now();
    const module=await WebAssembly.compile(b);const instance=await WebAssembly.instantiate(module,{env:{memory,
     tlb_read32:()=>{throw Error('Unexpected read fallback')},tlb_write32:()=>{throw Error('Unexpected write fallback')},
     tlb_read8:()=>{throw Error('read8')},tlb_write8:()=>{throw Error('write8')},tlb_read16:()=>{throw Error('read16')},tlb_write16:()=>{throw Error('write16')}}});
    compilation[suffix||'current']=performance.now()-begin;variants[suffix||'current']=instance.exports.run;
   }
   function init(seed:number) {
    bytes.fill(0);let x=seed;
    for(let a=0x10000;a<0x19000;a+=4){x=(Math.imul(x,1664525)+1013904223)>>>0;u[a/4]=x;}
    for(let r=0;r<16;++r)u[(s+4*r)/4]=0x13000+r*64;
    u[s/4]=0x12000;u[s/4+1]=0x10000;u[s/4+2]=0x11000;u[s/4+4]=0x13000;u[s/4+5]=0x14000;u[s/4+13]=0x18000;u[s/4+14]=0x1a000;u[s/4+15]=pc;
    u[(s+784)/4]=16;u[(s+796)/4]=16;u[(s+848)/4]=cycles;u[(s+852)/4]=8192;u[(s+856)/4]=0x70000000;u[(s+860)/4]=0x70080000;u[(s+876)/4]=1;
    for(let a=0x10000;a<=0x19000;a+=4096){const e=(8192+((a>>>12)&511)*16)/4;u[e]=u[e+1]=u[e+2]=u[e+3]=a;}
   }
   let comparisons=0;
   for(let seed=1;seed<=64;++seed){init(seed);const n=variants.current(s);if(n!==cycles)throw Error(`current count ${n} != ${cycles}`);const expected=bytes.slice(0,0x1b000);
    for(const name of ['opt','ir','ir-opt','whole']){init(seed);const got=variants[name](s);if(got!==cycles)throw Error(`${name} count ${got}`);for(let a=0;a<expected.length;++a)if(bytes[a]!==expected[a])throw Error(`${pc} ${name} seed ${seed} mismatch ${a}: ${bytes[a]} vs ${expected[a]}`);comparisons++;}}
   if(validateOnly){all.push({pc,cycles,comparisons,compilation});continue;}
   const drivers:Record<string,any>={};
   const driverModule=await WebAssembly.compile(await(await fetch('/driver.wasm')).arrayBuffer());
   for(const [name,run]of Object.entries(variants)){drivers[name]=(await WebAssembly.instantiate(driverModule,{env:{memory,run}})).exports.loop;}
   init(72);bytes.copyWithin(4096,s,s+64);
   // Warm optimizing compilation without debugger/profiler involvement.
   for(const run of Object.values(drivers))run(500000);
   await new Promise(r=>setTimeout(r,1000));
   const timings:any[]=[];
   for(let round=0;round<6;++round){const names=round%2?Object.keys(drivers).reverse():Object.keys(drivers);for(const name of names){init(72);bytes.copyWithin(4096,s,s+64);const begin=performance.now();const count=drivers[name](5000000);const ms=performance.now()-begin;if(count!==5000000*cycles)throw Error('Driver count');timings.push({round,name,ms});}}
   all.push({pc,cycles,comparisons,compilation,timings});
  }
  return {userAgent:navigator.userAgent,all};
 },process.env.EKA_PROBE_VALIDATE_ONLY === "1");
 fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}finally{await browser.close();server.close();}
