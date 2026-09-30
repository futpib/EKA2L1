import fs from 'node:fs';import http from 'node:http';import path from 'node:path';import{createRequire}from'node:module';
const require=createRequire(new URL('../../wasm/package.json',import.meta.url));const puppeteer=require('puppeteer');
const root=process.argv[2],reference=JSON.parse(fs.readFileSync(process.argv[3])),output=process.argv[4];
const server=http.createServer((req,res)=>{const name=new URL(req.url,'http://x').pathname;res.setHeader('Cross-Origin-Opener-Policy','same-origin');res.setHeader('Cross-Origin-Embedder-Policy','require-corp');res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':'text/javascript');if(name==='/'){res.setHeader('Content-Type','text/html');return res.end('<!doctype html><title>Flycast CPU comparison</title>');}try{res.end(fs.readFileSync(path.join(root,name)));}catch{res.statusCode=404;res.end();}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
const report={browser:await browser.version(),scope:'SH4 algorithm ports, decoded blocks plus production C dispatcher, no console scheduler or adaptive promotion',errors:[],observations:[]};
try{for(let round=0;round<2;round++){
 const page=await browser.newPage();page.on('console',m=>console.log(m.text()));page.on('pageerror',e=>report.errors.push(String(e)));await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.addScriptTag({url:'/flycast.js'});
 const row=await page.evaluate(async({refs,round})=>{
  const begin=performance.now();const m=await createFlycast();const initialization_ms=performance.now()-begin;const results=[];
  function run(k,n,phase){const t0=performance.now();if(!m._bench_setup(k,n))throw Error('setup failed '+k);const t1=performance.now();const ok=m._bench_run();const t2=performance.now();const indices=[0,1,3,4,5,16];const got=indices.map(i=>m._bench_result(i)>>>0);const e=refs[n][k];const expected=[e[0],e[1],e[3],e[4],e[5],e.at(-1)];if(!ok||JSON.stringify(got)!==JSON.stringify(expected))throw Error(JSON.stringify({k,n,phase,ok,got,expected,pc:m._bench_result(17)>>>0}));results.push({kind:k,n,phase,setup_ms:t1-t0,ms:t2-t1,got});}
  for(const k of round?[3,2,1,0]:[0,1,2,3]){run(k,1000000,'first');for(const n of [1,2,31,1000])run(k,n,'check');for(let j=0;j<3;j++)run(k,1000000,'warmup');for(let j=0;j<5;j++)run(k,1000000,'measure');}
  return {round,initialization_ms,results};
 },{refs:reference.references,round});report.observations.push(row);fs.writeFileSync(output,JSON.stringify(report,null,2));await page.close();
}}catch(e){report.errors.push(String(e));console.error(e);}finally{fs.writeFileSync(output,JSON.stringify(report,null,2));await browser.close();server.close();}
