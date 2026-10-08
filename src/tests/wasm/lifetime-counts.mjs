// Count executed guest-translation WASM operations, including private fallback
// functions. Counter instructions and structural block/loop/end/else markers
// are excluded. These instrumented modules are never used for CPU timings.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {counted,resetCost,readCost} from './wasm-step-counter.mjs';
import {CostComparisons} from './wasm-cost-model.mjs';

const [beforePath, afterPath, output] = process.argv.slice(2);
const breakdown=process.argv.includes('--breakdown'),costs=new CostComparisons();
if (!output) throw Error('lifetime-counts.mjs BASELINE_PROBES CANDIDATE_PROBES OUTPUT.json');
function readProbes(path) {
    const lines=fs.readFileSync(path,'utf8').split('\n');
    return {layout:JSON.parse(lines.find(l=>l.startsWith('MEMORY_LAYOUT ')).slice(14)),
        probes:lines.filter(l=>l.startsWith('MEMORY_PROBE ')).map(l=>JSON.parse(l.slice(13)))};
}
const before=readProbes(beforePath),after=readProbes(afterPath);
assert.deepEqual(before.layout,after.layout);assert.equal(before.probes.length,after.probes.length);
const S=before.layout, state=0x1000, tlb=0x2000, view=0x6000, pages=0x100000;
const memory=new WebAssembly.Memory({initial:2304,maximum:65536,shared:true});
const bytes=new Uint8Array(memory.buffer),words=new Uint32Array(memory.buffer),data=new DataView(memory.buffer);
const physical=a=>a>=0x400000&&a<0x4400000?a+0x3c00000:a>=0x7000&&a<0xc000?a+0x1000000:0;
const mapped=[0x7000,0x8000,0x9000,0xa000,0xb000,0x407000,0x408000,0x409000,0x40a000,0x40b000];
let helperTrace=[];
function helper(write,width,p,a,value) {
    a>>>=0;helperTrace.push([write,width,p,a,value,words[(state+60)/4]]);
    const host=physical(a);if(!host)return 0;
    if(write) {if(width===4)data.setUint32(host,value,true);else if(width===2)data.setUint16(host,value,true);else data.setUint8(host,value);return;}
    return width===4?data.getUint32(host,true):width===2?data.getUint16(host,true):data.getUint8(host);
}
const env={memory,tlb_read32:(p,a)=>helper(false,4,p,a),tlb_write32:(p,a,v)=>helper(true,4,p,a,v),
    tlb_read16:(p,a)=>helper(false,2,p,a),tlb_write16:(p,a,v)=>helper(true,2,p,a,v),
    tlb_read8:(p,a)=>helper(false,1,p,a),tlb_write8:(p,a,v)=>helper(true,1,p,a,v)};
function init(probe,scenario,budget,z) {
    bytes.fill(0,state,state+1024);bytes.fill(0,tlb,tlb+8192);
    const address=scenario.address??0x8040,permission=scenario.permission??3;
    for(const guest of mapped) {
        const host=physical(guest);
        for(let off=0;off<4096;off+=4)data.setUint32(host+off,Math.imul(guest+off,37),true);
        const entry=(tlb+((guest>>>12)&511)*16)/4;
        // The arena and ordinary fixture pages collide in the small TLB.
        // Publish the address range exercised by this invocation last/only.
        if((guest>=0x400000)===(address>=0x400000)) {
            words[entry]=permission&1?guest:0;words[entry+1]=permission&2?guest:0;
            words[entry+3]=scenario.zeroHost?0:host;
        }
        const page=(pages+(guest>>>12)*8)/4;
        words[page]=permission&1&&!scenario.zeroHost?host:0;
        words[page+1]=permission&2&&!scenario.zeroHost?host:0;
    }
    for(let r=0;r<16;r++)words[state/4+r]=0xabc00000+r;
    words[state/4]=scenario.alias?address+32:(address>=0x400000?0x40a040:0xa040);
    words[state/4+1]=address;words[state/4+2]=address+128;words[state/4+6]=2;
    words[state/4+13]=address>=0x400000?0x40b800:0xb800;words[state/4+14]=0x2000;words[state/4+15]=0x1000;
    words[view/4]=0x400000;words[view/4+1]=0x4000000;words[view/4+2]=0x4000000;
    words[view/4+3]=pages;words[view/4+4]=0x3c00000;words[view/4+5]=scenario.arena?0xffffffff:0;
    const field=(name,value)=>words[(state+S[name])/4]=value;
    field('mode',16);field('cpsr',16|(probe.thumb?32:0)|(scenario.endian?512:0));
    field('thumb',probe.thumb);field('z',z);field('budget',budget);field('irq',1);field('remaining',budget);
    field('tlb',scenario.nullView?0:probe.mode===2?view:tlb);
    field('code_begin',scenario.codeAlias?physical(words[state/4]):0x70000000);field('code_end',scenario.codeAlias?physical(words[state/4])+64:0x70001000);
    helperTrace=[];
}
const scenarios=[{name:'valid'},{name:'alias',alias:true},{name:'code-alias',codeAlias:true},{name:'read-only',permission:1},{name:'write-only',permission:2},
    {name:'denied',permission:0},{name:'null-view',nullView:true},{name:'zero-host',zeroHost:true},
    {name:'endian',endian:true},{name:'unaligned',address:0x8001},{name:'cross-page',address:0x8ffc},
    {name:'word-cross-page',address:0x8fff},{name:'half-cross-page',address:0x8ffe},
    {name:'page-zero',address:0},{name:'address-wrap',address:0xfffffffe},{name:'arena',address:0x408040,arena:true},{name:'arena-alias',address:0x408040,arena:true,alias:true},
    {name:'arena-cross-page',address:0x408ffc,arena:true}];
const rows=[];let reductions=0,unchanged=0,grown=0;
for(let i=0;i<before.probes.length;i++) {
    const a=before.probes[i],b=after.probes[i];assert.equal(a.name,b.name);assert.equal(a.mode,b.mode);
    const raw=[a,b].map(p=>Buffer.from(p.wasm,'base64'));
    const instances=raw.map(blob=>new WebAssembly.Instance(counted(blob,{breakdown}),{env}));
    let improved=0,comparisons=0;
    for(const scenario of scenarios)for(const budget of [0,1,3,4,8,16,32,57,64])for(const z of [0,1]) {
        const results=instances.map((instance,variant)=>{
            init(variant?b:a,scenario,budget,z);resetCost(instance);
            const count=instance.exports.f_4096(state);
            const hash=crypto.createHash('sha256');
            hash.update(bytes.subarray(state,state+1024));
            for(const guest of mapped)hash.update(bytes.subarray(physical(guest),physical(guest)+4096));
            const cost=readCost(instance);
            return {steps:cost.operations,cost,count,hash:hash.digest('hex'),helpers:helperTrace};
        });
        const [old,current]=results,label=`${a.name} mode=${a.mode} ${scenario.name} budget=${budget} z=${z}`;
        if(breakdown)costs.add(`${a.name} mode=${a.mode}`,old.cost,current.cost,{scenario:scenario.name,budget,z});
        assert.equal(current.count,old.count,label+' guest count');assert.equal(current.hash,old.hash,label+' state/memory');
        assert.deepEqual(current.helpers,old.helpers,label+' helpers');

        if(current.steps<old.steps){improved++;reductions++;}else if(current.steps>old.steps)grown++;else unchanged++;
        comparisons++;
        rows.push({name:a.name,mode:a.mode,scenario:scenario.name,budget,z,before:old.steps,after:current.steps,guest_instructions:current.count,helper_calls:current.helpers.length});
    }
    if(a.mode===0||a.name.includes('entry0')||a.name.includes('backedge'))assert(raw[0].equals(raw[1]),`${a.name}: excluded path changed`);
    console.log('PASS',a.name,'mode',a.mode,improved,'reduced of',comparisons);
}
fs.writeFileSync(output,JSON.stringify({metric:'Executed WASM operations; block/loop/end/else markers and counter operations excluded; imported helper bodies excluded but call traces equal',reductions,unchanged,grown,rows,...(breakdown?{cost_classes:costs.fixtures}:{})},null,2)+'\n');
console.log('PASS',reductions,'reduced,',unchanged,'unchanged,',grown,'increased; state, memory, guest progress and helper traces match');
