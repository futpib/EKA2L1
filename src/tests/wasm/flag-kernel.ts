// Paired emitted-code probe. Full interpreter differential tests are separate.
import fs from 'node:fs';
import http from 'node:http';
import crypto from 'node:crypto';
import puppeteer from 'puppeteer';
const [before, after, output] = process.argv.slice(2);
if (!output) throw Error('flag-kernel.ts BASELINE_MODULE CANDIDATE_MODULE NEW_REPORT');
if (fs.existsSync(output)) throw Error('Output already exists');
const modules = [before, after].map(p => [...fs.readFileSync(p)]);
const server = http.createServer((_, res) => {
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.end('<!doctype html><title>Flag kernel probe</title>');
});
await new Promise<void>(r => server.listen(0, '127.0.0.1', r));
const browser = await puppeteer.launch({executablePath: '/usr/bin/chromium', headless: true, args: ['--no-sandbox']});
try {
 const page = await browser.newPage();
 await page.goto(`http://127.0.0.1:${(server.address() as any).port}`);
 const report = await page.evaluate(async modules => {
  const memory = new WebAssembly.Memory({initial:256,maximum:65536,shared:true});
  const env:any = {memory};
  for(const name of ['tlb_read32','tlb_write32','tlb_read8','tlb_write8','tlb_read16','tlb_write16']) env[name] = () => {throw Error('Unexpected memory helper');};
  const instances = await Promise.all(modules.map(async bytes => (await WebAssembly.instantiate(new Uint8Array(bytes),{env})).instance));
  const state = new Uint32Array(memory.buffer,0,256);
  function run(variant:number, kernel:number, budget:number, value=0x7fffffff) {
   state.fill(0);state[0]=value;state[2]=1000;state[15]=0x1000;
   state[784/4]=16;state[848/4]=budget;state[876/4]=1;
   const count=(instances[variant].exports[`probe${kernel}`] as any)(0);
   return count;
  }
  let comparisons=0;
  for(let kernel=0;kernel<2;++kernel) for(const value of [0,1,2,0x7fffffff,0x80000000,0xffffffff]) for(let budget=0;budget<=65;++budget) {
   const count=run(0,kernel,budget,value),expected=Array.from(state);
   if(run(1,kernel,budget,value)!==count || expected.some((v,i)=>v!==state[i])) throw Error(`Mismatch ${kernel}/${value}/${budget}`);
   ++comparisons;
  }
  const rows:any[]=[];
  for(let kernel=0;kernel<2;++kernel) {
   for(let variant=0;variant<2;++variant) for(let n=0;n<20000;++n) run(variant,kernel,4000);
   await new Promise(r=>setTimeout(r,1000));
   for(let round=0;round<8;++round) for(const variant of round%2?[1,0]:[0,1]) {
    let count=0;const start=performance.now();
    for(let n=0;n<20000;++n) count+=run(variant,kernel,4000);
    const ms=performance.now()-start;
    if(count!==80000000) throw Error('Incorrect instruction count');
    rows.push({kernel,round,variant,ms,instructions:count});
   }
  }
  return {comparisons,rows,userAgent:navigator.userAgent};
 },modules);
 fs.writeFileSync(output,JSON.stringify({...report,browser:await browser.version(),files:[before,after].map(p=>({path:p,sha256:crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')}))},null,2)+'\n');
 console.log(JSON.stringify(report));
} finally {await browser.close();server.close();}
