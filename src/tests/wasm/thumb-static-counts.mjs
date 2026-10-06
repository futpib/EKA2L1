// Compare complete compiled chains with equal guest work and callback traces.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {counted} from './wasm-step-counter.mjs';

const [input,output]=process.argv.slice(2);
if(!output)throw Error('thumb-static-counts.mjs PROBES.log_OR_REPORT.json OUTPUT.json');
const source=fs.readFileSync(input,'utf8');
const probes=source.trimStart().startsWith('{')?JSON.parse(source).probes:
    source.split('\n').filter(l=>l.startsWith('THUMB_REGIONS ')).map(l=>JSON.parse(l.slice(14)));
assert(Array.isArray(probes)&&probes.length>0&&probes.length%2===0,'Expected nonempty baseline/candidate probe pairs');
const memory=new WebAssembly.Memory({initial:256,maximum:65536,shared:true});
const bytes=new Uint8Array(memory.buffer),words=new Uint32Array(memory.buffer),data=new DataView(memory.buffer);
const state=0x1000, host=0x100000, view=0x6000, pages=0x200000;
let helpers=[],callback='none';
function access(write,width,p,a,v) {
    a>>>=0;
    if(helpers.length===(callback.startsWith('callback-late')?1:0)) {
        if(callback==='callback-stop')words[(p+840)/4]=0;
        if(callback==='callback-irq')words[(p+876)/4]=0;
        if(callback==='callback-endian'||callback==='callback-late-endian')words[(p+784)/4]|=512;
        if(callback==='callback-late-register'){words[p/4]=7;words[(p+812)/4]=1;}
        if(callback==='callback-map')words[(p+852)/4]=view;
    }
    helpers.push([write,width,a,v,[...words.subarray(p/4,p/4+16)],...words.subarray((p+804)/4,(p+820)/4)]);
    const address=host+(a>=0x400000 ? a-0x400000 : a);
    if(write){if(width===4)data.setUint32(address,v,true);else if(width===2)data.setUint16(address,v,true);else data.setUint8(address,v);return;}
    return width===4?data.getUint32(address,true):width===2?data.getUint16(address,true):data.getUint8(address);
}
const env={memory,tlb_read32:(p,a)=>access(false,4,p,a),tlb_write32:(p,a,v)=>access(true,4,p,a,v),
    tlb_read16:(p,a)=>access(false,2,p,a),tlb_write16:(p,a,v)=>access(true,2,p,a,v),
    tlb_read8:(p,a)=>access(false,1,p,a),tlb_write8:(p,a,v)=>access(true,1,p,a,v)};
const rows=[];
for(let k=0;k<probes.length;k+=2) {
    assert.equal(probes[k].variant,0);assert.equal(probes[k+1].variant,1);assert.equal(probes[k].name,probes[k+1].name);
    const instances=probes.slice(k,k+2).map(p=>new WebAssembly.Instance(counted(Buffer.from(p.wasm,'base64')),{env}));
    const name=probes[k].name, isStatic=probes[k+1].accepted!==undefined?probes[k+1].accepted>0:/^(long_|tail_|condition_)/.test(name);
    if(!isStatic)assert.equal(probes[k].wasm,probes[k+1].wasm,'Rejected fixture must remain byte-identical: '+name);
    else assert.notEqual(probes[k].wasm,probes[k+1].wasm,'Fixture must exercise the optimization: '+name);
    const flags=name.startsWith('condition_')?[...Array(16).keys()]:[0];
    const budgets=[...Array(41).keys(),100,...(isStatic?[0x7fffffff,0xffffffff]:[])];
    for(const flag of flags)for(const seed of [0,3])for(const budget of budgets)for(const mapping of ['helper','page','arena'])for(const stop of ['none','stopped','pending','masked','high-word','callback-stop','callback-irq','callback-endian','callback-late-endian','callback-late-register','callback-map']) {
        const results=instances.map(instance=>{
            bytes.fill(0,state,state+1024);bytes.fill(0,host+0x7000,host+0x9000);helpers=[];callback=stop;
            words[host/4+0x8000/4]=seed;
            words[state/4]=words[state/4+1]=seed;
            words[state/4+3]=words[state/4+5]=words[state/4+13]=mapping==='arena'?0x408000:0x8000;
            words[state/4+14]=0x2001;words[state/4+15]=0x1000;
            words[(state+784)/4]=48|(flag<<28);for(let i=0;i<4;i++)words[(state+804)/4+i]=(flag>>>(3-i))&1;words[(state+796)/4]=16;words[(state+828)/4]=1;
            words[(state+840)/4]=stop==='stopped'||stop==='high-word'?0:budget;words[(state+844)/4]=stop==='high-word'?1:0;
            words[(state+876)/4]=stop==='pending'||stop==='masked'?0:1;if(stop==='masked')words[(state+784)/4]|=128;
            words[(state+852)/4]=mapping==='helper'?0:view;
            words[view/4]=0x400000;words[view/4+1]=0x4000000;words[view/4+2]=host;
            words[view/4+3]=pages;words[view/4+4]=(host-0x400000)>>>0;words[view/4+5]=mapping==='arena'?0xffffffff:0;
            for(const page of [7,8]) {words[(pages+page*8)/4]=host+page*4096;words[(pages+page*8)/4+1]=host+page*4096;}
            instance.exports.steps.value=0n;
            let executed=0,calls=0;
            while(calls<1000) {
                // Real compiled entry follows the outer loop's stop/IRQ checks.
                if(probes[k+1].accepted!==undefined && (!(words[(state+840)/4]||words[(state+844)/4]) || (!words[(state+876)/4] && !(words[(state+784)/4]&128))))break;
                const fn=instance.exports['f_'+(words[state/4+15]&~1)];
                if(!fn || (calls && executed>=budget))break;
                words[(state+848)/4]=budget-executed;
                const n=fn(state)>>>0;assert(n<=budget-executed);
                executed+=n;calls++;if(!n || !(words[(state+840)/4]||words[(state+844)/4]) || (!words[(state+876)/4] && !(words[(state+784)/4]&128)))break;
            }
            const steps=Number(instance.exports.steps.value);
            if(mapping!=='helper')assert.equal(helpers.length,0, mapping+' must stay on its mapped fast path');
            words[(state+848)/4]=0;
            return {steps,executed,calls,helpers,state:[...words.subarray(state/4,state/4+256)],memory:Buffer.from(bytes.subarray(host+0x7000,host+0x9000))};
        });
        const [a,b]=results,label=JSON.stringify({name,flag,seed,budget,mapping,stop});
        assert(b.calls<=a.calls,label);assert.equal(a.executed,b.executed,label);assert.deepEqual(a.state,b.state,label);
        assert.deepEqual(a.memory,b.memory,label);assert.deepEqual(a.helpers,b.helpers,label);
        assert(b.steps<=a.steps,`${label}: grew ${a.steps} -> ${b.steps}`);
        rows.push({name,flag,seed,budget,mapping,stop,before:a.steps,after:b.steps,before_calls:a.calls,after_calls:b.calls,guest_instructions:a.executed});
    }
}
const summary={reduced:rows.filter(r=>r.after<r.before).length,unchanged:rows.filter(r=>r.after===r.before).length,increased:rows.filter(r=>r.after>r.before).length};
fs.writeFileSync(output,JSON.stringify({summary,rows},null,2)+'\n');console.log(summary);
for(const name of [...new Set(rows.map(r=>r.name))])console.log(name,rows.filter(r=>r.name===name&&r.flag===0&&r.budget===100&&r.mapping==='page'&&r.stop==='none').map(r=>[r.seed,r.before,r.after,r.before_calls,r.after_calls]));
