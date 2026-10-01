// A single browser, same C++ batching loop for the compact reference and current emitter.
import crypto from 'node:crypto';import fs from 'node:fs';import path from 'node:path';import http from 'node:http';import puppeteer from 'puppeteer';
const [build,fixtures,output]=process.argv.slice(2);if(!output)throw Error('matched-kernel.ts BUILD FIXTURES NEW_OUTPUT');fs.mkdirSync(output);
const server=http.createServer((req,res)=>{const name=path.basename(req.url||'');res.setHeader('Cross-Origin-Opener-Policy','same-origin');res.setHeader('Cross-Origin-Embedder-Policy','require-corp');if(name==='favicon.ico'){res.writeHead(204).end();return;}if(!name){res.end('<script src="/eka_matched_kernel.js"></script>');return;}const file=path.join(name==='native-fixtures.json'?fixtures:build,name);if(!fs.existsSync(file)){res.writeHead(404).end();return;}if(name.endsWith('.wasm'))res.setHeader('Content-Type','application/wasm');fs.createReadStream(file).pipe(res);});
await new Promise<void>(r=>server.listen(0,'127.0.0.1',r));const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox',...(process.env.EKA2L1_V8_FLAGS?[`--js-flags=${process.env.EKA2L1_V8_FLAGS}`]:[])],protocolTimeout:600000});
try{const page=await browser.newPage();page.on('console',m=>console.log(m.text()));page.on('pageerror',e=>console.error(e));await page.goto(`http://127.0.0.1:${(server.address() as any).port}`);
const result=await page.evaluate(async(inspect)=>{
 const m=await (window as any).createMatchedKernel();const fixtures=await(await fetch('/native-fixtures.json')).json(),all:any[]=[];
 const edgeComparisons=m._check_edges();
 for(let k=0;k<2;++k){m._select_kernel(k);const pc=k?1879129820:1879455500,cycles=k?7:57;let comparisons=0;
 for(const f of fixtures[pc])for(let variant=0;variant<3;++variant){m._reset(f.seed,f.budget);const mem=m.HEAPU8,base=m._data_pointer(),regs=m._register_pointer();const expected=mem.slice(base,base+0x20000);for(const [off,value]of f.changes)expected[off]=value;
 const count=m._invoke(variant);if(count!==f.budget)throw Error(`${pc}/${variant} budget ${f.budget} => ${count}`);const u=m.HEAPU32;
 for(let r=0;r<16;++r)if(u[(regs>>2)+r]!==f.regs[r])throw Error(`${pc}/${variant} seed ${f.seed}/${f.budget} r${r}: ${u[(regs>>2)+r]} != ${f.regs[r]}`);
 if((m._cpsr_value()>>>0)!==f.cpsr)throw Error('CPSR mismatch');for(let a=0;a<expected.length;++a)if(mem[base+a]!==expected[a])throw Error(`Memory mismatch ${pc}/${variant} ${f.seed}/${f.budget} at ${a}`);comparisons++;}
 for(let variant=0;variant<3;++variant){m._reset(72,cycles);m._batch(variant,500000);}await new Promise(r=>setTimeout(r,1000));
 const timings:any[]=[];for(let round=0;round<(inspect?1:8);++round)for(const variant of(round%2?[2,1,0]:[0,1,2])){m._reset(72,cycles);const start=performance.now();const count=m._batch(variant,5000000),ms=performance.now()-start;if(count!==cycles*5000000)throw Error('Batch count');timings.push({round,variant,ms});}
 all.push({pc,cycles,calls:5000000,comparisons,timings});
 }return{userAgent:navigator.userAgent,edgeComparisons,all};},!!process.env.EKA2L1_V8_FLAGS);(result as any).wasm_sha256=crypto.createHash('sha256').update(fs.readFileSync(path.join(build,'eka_matched_kernel.wasm'))).digest('hex');(result as any).browser=await browser.version();(result as any).inspection=!!process.env.EKA2L1_V8_FLAGS;fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}finally{await browser.close();server.close();}
