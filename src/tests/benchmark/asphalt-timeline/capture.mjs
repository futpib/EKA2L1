import fs from 'node:fs';
import {spawn} from 'node:child_process';
import {sampleCpuTime,cpuTimeDelta} from '../../wasm/cpu-time.ts';
export async function capture({page,browser,out,measure},label,seconds=8){
 const system=await browser.target().createCDPSession();
 const {processInfo}=await system.send('SystemInfo.getProcessInfo');
 const pids=processInfo.filter(p=>p.type==='renderer').map(p=>p.id);
 const sampler=spawn('python3',[new URL('sample_switches.py',import.meta.url).pathname,out+'/'+label+'-switches.json',...pids.map(String)],{stdio:['pipe','pipe','pipe']});
 let stderr='',stdout='';sampler.stderr.on('data',c=>stderr+=c);
 const done=new Promise((resolve,reject)=>{sampler.on('exit',code=>code===0?resolve():reject(Error(stderr)));sampler.on('error',reject)});done.catch(()=>{});
 await Promise.race([done.then(()=>{throw Error('Sampler exited before READY')}),new Promise(resolve=>sampler.stdout.on('data',c=>{stdout+=c;if(stdout.includes('READY'))resolve()}))]);
 const anchors=[];
 for(let i=0;i<20;i++){const before=Number(process.hrtime.bigint());const host=await page.evaluate(()=>performance.timeOrigin+performance.now());const after=Number(process.hrtime.bigint());anchors.push({before,after,host_ms:host});}
 const before=await sampleCpuTime(system);
 await page.evaluate(()=>Module._eka_trace_control(1));
 let measurement;
 try { measurement=await measure(label,seconds); }
 finally { await page.evaluate(()=>Module._eka_trace_control(0)); sampler.stdin.end('\n');await done; }
 const after=await sampleCpuTime(system);await system.detach();
 fs.writeFileSync(out+'/'+label+'-capture-meta.json',JSON.stringify({measurement,cpu_time:cpuTimeDelta(before,after),anchors},null,2));
 await new Promise(r=>setTimeout(r,100));
 const data=await page.evaluate(()=>{
  const n=Module._eka_trace_control(2),capacity=Module._eka_trace_control(4),p=Module._eka_trace_control(3);
  const bytes=HEAPU8.slice(p,p+Math.min(n,capacity)*64);let s='';
  for(let i=0;i<bytes.length;i+=8192)s+=String.fromCharCode(...bytes.subarray(i,i+8192));
  const view=new DataView(bytes.buffer),names={};
  for(let i=0;i<bytes.length;i+=64){const k=Number(view.getBigUint64(i+16,true));if(k===9||k===11){const addr=Number(view.getBigUint64(i+56,true));if(addr && !names[addr]){let end=addr;while(HEAPU8[end]&&end<addr+256)end++;names[addr]=new TextDecoder().decode(HEAPU8.slice(addr,end));}}}
  return {n,capacity,base64:btoa(s),names};
 });
 fs.writeFileSync(out+'/'+label+'.trace',Buffer.from(data.base64,'base64'));
 const result={measurement,cpu_time:cpuTimeDelta(before,after),count:data.n,capacity:data.capacity,names:data.names,anchors};
 fs.writeFileSync(out+'/'+label+'-trace.json',JSON.stringify(result,null,2));
 return {...result,cpu_time:undefined};
}
