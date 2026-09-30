import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {createRequire} from 'node:module';
const require=createRequire(new URL('../../wasm/package.json',import.meta.url));
const puppeteer=require('puppeteer');
const root=process.argv[2];
const output=process.argv[3];
const n=Number(process.env.CORE_ITERATIONS||1000000);
const kinds=Number(process.env.CORE_KINDS||3);
const candidates=(process.env.CORE_NAMES||'skyemu7,skyemu9,rpcemu').split(',');
function reference(kind,n){
 let r=Array(15).fill(0),mem=Array.from({length:kind===3?16384:256},(_,i)=>kind===3?((i*109+1021)&16383)*4:(Math.imul(i,2654435761)+17)>>>0);
 r[0]=0x12345678;r[1]=n;r[2]=65536;r[3]=0x9abcdef0;
 const ror=(a,n)=>((a>>>n)|(a<<(32-n)))>>>0;
 while(r[1]){
  if(kind===0){r[0]=(r[0]^(r[0]<<13))>>>0;r[0]=(r[0]^(r[0]>>>17))>>>0;r[0]=(r[0]^(r[0]<<5))>>>0;r[3]=(r[3]+r[0])>>>0;r[4]=ror(r[3],7);r[0]=(r[0]^r[4])>>>0;}
  else if(kind===1){r[4]=r[1]&255;r[5]=mem[r[4]];r[0]=(r[0]+r[5])>>>0;r[5]=(r[0]^ror(r[0],11))>>>0;mem[r[4]]=r[5];r[3]=(r[3]+r[5])>>>0;}
  else if(kind===3){r[4]=mem[r[4]>>>2];r[0]=(r[0]+r[4])>>>0;r[3]=(r[3]^ror(r[0],9))>>>0;r[5]=mem[r[4]>>>2];r[0]=(r[0]+r[5])>>>0;r[4]=r[5];}
  else {if(r[0]&1)r[3]=(r[3]^r[0])>>>0;else r[3]=(r[3]+r[0])>>>0;r[4]=ror(r[0],1);r[0]=(r[4]^r[3])>>>0;r[0]=(r[0]+(r[0]<r[3]?31:-7))>>>0;}
  --r[1];
 }
 r[10]=1;let h=2166136261;for(const v of mem)h=Math.imul(h^v,16777619)>>>0;
 return [...r,h];
}
const counts=[1,2,31,1000,n];
const references=Object.fromEntries(counts.map(c=>[c,Array.from({length:kinds},(_,k)=>reference(k,c))]));
const server=http.createServer((req,res)=>{
 const file=path.join(root,decodeURIComponent(new URL(req.url,'http://x').pathname));
 res.setHeader('Cross-Origin-Opener-Policy','same-origin');res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
 if(req.url==='/'){res.setHeader('Content-Type','text/html');return res.end('<!doctype html><title>CPU benchmark</title>');}
 try{res.setHeader('Content-Type',file.endsWith('.wasm')?'application/wasm':'text/javascript');res.end(fs.readFileSync(file));}catch{res.statusCode=404;res.end();}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
const report={browser:await browser.version(),n,counts,references,observations:[],errors:[]};
try{
 for(const order of [candidates,[...candidates].reverse()])for(const name of order){
  const page=await browser.newPage();page.on('pageerror',e=>report.errors.push(String(e)));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  const script=name.startsWith('skyemu')?'skyemu':name;
  await page.addScriptTag({url:`/${script}.js`});
  const row=await page.evaluate(async({name,n,counts,refs,kinds})=>{
   const start=performance.now();const m=await createCore();const initialization_ms=performance.now()-start;
   const selector=name==='skyemu9'?1:0;const results=[];let lastSetup=0;
   function run(k,c){const setupStart=performance.now();m._setup(k,c);const t=performance.now();lastSetup=t-setupStart;const ok=m._run(selector);const ms=performance.now()-t;const got=[...Array.from({length:15},(_,i)=>m._result(i)>>>0),m._result(16)>>>0];if(!ok||JSON.stringify(got)!==JSON.stringify(refs[c][k]))throw Error(JSON.stringify({name,k,c,ok,got,expected:refs[c][k]}));return ms;}
   results.push({phase:'first_full_run',k:0,c:n,ms:run(0,n),setup_ms:lastSetup});
   for(const c of counts)for(let k=0;k<kinds;k++)results.push({phase:'check',k,c,ms:run(k,c)});
   for(let k=0;k<kinds;k++){for(let j=0;j<3;j++)results.push({phase:'warmup',k,c:n,ms:run(k,n)});for(let j=0;j<5;j++)results.push({phase:'measure',k,c:n,ms:run(k,n)});}
   return {name,initialization_ms,results};
  },{name,n,counts,refs:references,kinds});
  report.observations.push(row);fs.writeFileSync(output,JSON.stringify(report,null,2));console.log(name,row.results.filter(x=>x.phase==='measure').map(x=>x.ms.toFixed(2)).join(','));
  await page.close();
 }
}finally{await browser.close();server.close();fs.writeFileSync(output,JSON.stringify(report,null,2));}
