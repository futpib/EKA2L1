// Exercise OPFS initialization with both new tabs and cached-page reloads.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import puppeteer from 'puppeteer';
const [input,output] = process.argv.slice(2);
assert(input&&output,'Usage: node opfs-startup.test.mjs URL_OR_FIXTURE_DIR NEW_OUTPUT');
fs.mkdirSync(output);
let url=input, server;
if (!/^https?:/.test(input)) {
  server=http.createServer((request,response)=>{
    const file=path.join(input,path.basename(new URL(request.url,'http://localhost').pathname));
    if(!fs.existsSync(file)||!fs.statSync(file).isFile()){response.writeHead(404).end();return;}
    response.writeHead(200,{'Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp',
      'Content-Type':file.endsWith('.wasm')?'application/wasm':file.endsWith('.js')?'application/javascript':'text/html'});
    fs.createReadStream(file).pipe(response);
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  url=`http://127.0.0.1:${server.address().port}/test_device_disk.html`;
}
const profile=fs.mkdtempSync(path.join(output,'profile-'));
const browser=await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,userDataDir:profile,
  args:['--no-sandbox','--disable-dev-shm-usage']});
const report={runs:[],errors:[]};
let page;
try {
  for(let i=0;i<20;i++) {
    if(i%2===0) {
      await page?.close();page=await browser.newPage();
      page.on('pageerror',error=>report.errors.push(String(error)));
    }
    await page.goto(new URL('?game=custom',url).href,{waitUntil:'domcontentloaded'});
    await page.waitForFunction(()=>window.Module?.calledRun&&Module._eka2l1_opfs_status()===1,{timeout:15000});
    report.runs.push({iteration:i+1,ms:await page.evaluate(()=>performance.now())});
    assert.deepEqual(report.errors,[]);
  }
  report.complete=true;
} catch(error) {report.failure=String(error);throw error;}
finally {
  await browser.close();fs.rmSync(profile,{recursive:true,force:true});
  server?.close();
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify(report));
}
