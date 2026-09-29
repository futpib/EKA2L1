// Bounded cold-corpus experiment. Does not claim emulator integration or execute
// guest code: measures loading/compilation and scheduling separately.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import http from 'node:http';
import puppeteer from 'puppeteer';
const [corpus,out] = process.argv.slice(2);
if (!corpus || !out || fs.existsSync(out)) throw new Error('Usage: compile-scheduling.ts CORPUS NEW_OUTPUT.json');
const files=fs.readdirSync(corpus).filter(x=>x.endsWith('.wasm')).sort();
if (!files.length) throw new Error('Empty module corpus');
const bytes=files.map(x=>fs.readFileSync(path.join(corpus,x)));
const server=http.createServer((req,res)=>{res.setHeader('Cross-Origin-Opener-Policy','same-origin');res.setHeader('Cross-Origin-Embedder-Policy','require-corp'); const i=Number(req.url?.slice(1));if(req.url==='/' ){res.end('<!doctype html><title>Compilation scheduling</title>');}else if(Number.isInteger(i)&&i>=0&&i<bytes.length){res.end(bytes[i]);}else{res.statusCode=404;res.end();}});
await new Promise<void>(r=>server.listen(0,'127.0.0.1',r));
const port=(server.address() as any).port;const rows=[];
try{
 for(const mode of ['sync','async','async','sync']) {
  const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
  try{const page=await browser.newPage();await page.goto(`http://127.0.0.1:${port}/`);
   const result=await page.evaluate(async ({count,mode})=>{
    const data=await Promise.all(Array.from({length:count},(_,i)=>fetch('/'+i).then(x=>x.arrayBuffer())));
    const experiment = async (data: ArrayBuffer[], mode: string) => {
    let last=performance.now(), maxGap=0,ticks=0;
    const timer=setInterval(()=>{const now=performance.now();maxGap=Math.max(maxGap,now-last);last=now;++ticks;},5);
    await new Promise(r=>setTimeout(r,30));ticks=0;maxGap=0;last=performance.now();
    const start=performance.now();
    const modules=mode==='sync'?data.map(x=>new WebAssembly.Module(x)):await Promise.all(data.map(x=>WebAssembly.compile(x)));
    const compileMs=performance.now()-start,compileTicks=ticks;
    await new Promise(r=>setTimeout(r,20));clearInterval(timer);
    const memory=new WebAssembly.Memory({initial:256,maximum:32768,shared:true});
    const env={memory,tlb_read32:()=>0,tlb_write32:()=>{},tlb_read8:()=>0,tlb_write8:()=>{},tlb_read16:()=>0,tlb_write16:()=>{}};
    const instStart=performance.now();let exports=0;
    for(const module of modules)exports+=Object.keys(new WebAssembly.Instance(module,{env}).exports).length;
    return {mode,compile_ms:compileMs,instantiate_ms:performance.now()-instStart,compile_ticks:compileTicks,max_compiler_worker_gap_ms:maxGap,modules:modules.length,exports};
    };
    const worker=new Worker(URL.createObjectURL(new Blob(['onmessage=async e=>{try{postMessage(await ('+experiment.toString()+')(...e.data))}catch(e){postMessage({error:String(e)})}}'],{type:'text/javascript'})));
    let last=performance.now(),gap=0;const timer=setInterval(()=>{const now=performance.now();gap=Math.max(gap,now-last);last=now;},5);
    const result:any=await new Promise((resolve,reject)=>{worker.onmessage=e=>resolve(e.data);worker.onerror=reject;worker.postMessage([data,mode],data);});
    clearInterval(timer);worker.terminate();if(result.error)throw new Error(result.error);return {...result,max_main_thread_gap_ms:gap};
   },{count:files.length,mode});rows.push(result);console.log(result);
  }finally{await browser.close();}
 }
 if (rows.some(r=>r.modules!==files.length || r.exports!==rows[0].exports)) throw new Error('Module/export count mismatch');
 fs.writeFileSync(out,JSON.stringify({scope:'fresh browser per trial; corpus preloaded outside timing; compilation on dedicated worker; both worker and page heartbeat measured; stub imports for installation only; no guest execution',corpus:files.map((name,i)=>({name,bytes:bytes[i].length,sha256:crypto.createHash('sha256').update(bytes[i]).digest('hex')})),rows},null,2));
}finally{server.close();}
