// Count executed guest-translation WASM operations, including private fallback
// functions. Counter instructions and structural block/loop/end/else markers
// are excluded. These instrumented modules are never used for CPU timings.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';

const [beforePath, afterPath, output] = process.argv.slice(2);
if (!output) throw Error('span-counts.mjs BASELINE_PROBES CANDIDATE_PROBES OUTPUT.json');
const leb = n => { const out=[]; do { const b=n&127; n>>>=7; out.push(b|(n?128:0)); } while(n); return out; };
function reader(bytes, start=0) {
    return {bytes, pos:start, u() { let n=0, shift=0, b; do { b=this.bytes[this.pos++]; n|=(b&127)<<shift; shift+=7; } while(b&128); return n>>>0; },
        skipLeb() { while(this.bytes[this.pos++]&128); }};
}
function counted(bytes) {
    const r=reader(bytes,8), sections=[];
    while(r.pos<bytes.length) { const id=bytes[r.pos++], n=r.u(); sections.push([id,bytes.slice(r.pos,r.pos+n)]); r.pos+=n; }
    assert(!sections.some(([id])=>id===6),'Probe unexpectedly has globals');
    const out=[...bytes.slice(0,8)];
    const section=(id,data)=>out.push(id,...leb(data.length),...data);
    const increment=[0x23,0,0x42,1,0x7c,0x24,0];
    for(let [id,data] of sections) {
        if(id===7) {
            section(6,[1,0x7e,1,0x42,0,0x0b]); // mutable i64 global
            const e=reader(data), n=e.u();
            data=Uint8Array.from([...leb(n+1),...data.slice(e.pos),5,...Buffer.from('steps'),3,0]);
        }
        if(id===10) {
            const c=reader(data), functions=c.u(), code=[...leb(functions)];
            for(let f=0;f<functions;f++) {
                const size=c.u(), end=c.pos+size, start=c.pos, groups=c.u();
                for(let g=0;g<groups;g++){c.u();c.pos++;}
                const body=[...data.slice(start,c.pos)];
                while(c.pos<end) {
                    const begin=c.pos, op=data[c.pos++];
                    if([2,3,4,0x41,0x42].includes(op))c.skipLeb();
                    else if([0x0c,0x0d,0x10,0x20,0x21,0x22,0x23,0x24,0x3f,0x40].includes(op))c.u();
                    else if(op===0x0e){const n=c.u();for(let j=0;j<=n;j++)c.u();}
                    else if(op===0x11 || (op>=0x28&&op<=0x3e)){c.u();c.u();}
                    else if(op===0x43)c.pos+=4;
                    else if(op===0x44)c.pos+=8;
                    else assert([0,1,5,0x0b,0x0f,0x1a,0x1b].includes(op)||(op>=0x45&&op<=0xc4),`Unknown opcode ${op.toString(16)}`);
                    if(![2,3,5,0x0b].includes(op))body.push(...increment);
                    body.push(...data.slice(begin,c.pos));
                }
                assert.equal(c.pos,end);code.push(...leb(body.length),...body);
            }
            data=Uint8Array.from(code);
        }
        section(id,data);
    }
    return new WebAssembly.Module(Uint8Array.from(out));
}
function readProbes(path) {
    const lines=fs.readFileSync(path,'utf8').split('\n');
    return {layout:JSON.parse(lines.find(l=>l.startsWith('MEMORY_LAYOUT ')).slice(14)),
        probes:lines.filter(l=>l.startsWith('MEMORY_PROBE ')).map(l=>JSON.parse(l.slice(13)))};
}
// Check the counter independently on known branch, loop and call paths before
// using it to judge generated emulator code. Immediates are not instructions.
function checkCounter(bodies,arg,result,steps) {
    const raw=[0,97,115,109,1,0,0,0];
    const section=(id,b)=>raw.push(id,...leb(b.length),...b);
    section(1,[1,0x60,1,0x7f,1,0x7f]);section(3,[bodies.length,...bodies.map(()=>0)]);
    section(7,[1,5,...Buffer.from('probe'),0,0]);
    section(10,[bodies.length,...bodies.flatMap(body=>[...leb(body.length+1),0,...body])]);
    const instance=new WebAssembly.Instance(counted(Uint8Array.from(raw)));
    assert.equal(instance.exports.probe(arg),result);assert.equal(Number(instance.exports.steps.value),steps);
}
for(const condition of [0,1])checkCounter([[0x20,0,4,0x7f,0x41,7,5,0x41,9,0x0b,0x0b]],condition,condition?7:9,3);
checkCounter([[2,0x40,3,0x40,0x20,0,0x45,0x0d,1,0x20,0,0x41,1,0x6b,0x21,0,0x0c,0,0x0b,0x0b,0x20,0,0x0b]],3,0,28);
checkCounter([[0x20,0,0x10,1,0x0b],[0x20,0,0x41,0x7f,0x6a,0x0b]],8,7,5);
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
    words[state/4+1]=address;words[state/4+6]=2;words[state/4+13]=address;words[state/4+15]=0x1000;
    if(probe.thumb&&(probe.opcode&0xfe00)===0xb400) {
        let mask=(probe.opcode&255)|((probe.opcode&256)?16384:0),n=0;
        while(mask){n++;mask&=mask-1;}words[state/4+13]+=n*4;
    }
    words[view/4]=0x400000;words[view/4+1]=0x4000000;words[view/4+2]=0x4000000;
    words[view/4+3]=pages;words[view/4+4]=0x3c00000;words[view/4+5]=scenario.arena?0xffffffff:0;
    const field=(name,value)=>words[(state+S[name])/4]=value;
    field('mode',16);field('cpsr',16|(probe.thumb?32:0)|(scenario.endian?512:0));
    field('thumb',probe.thumb);field('z',z);field('budget',budget);field('irq',1);field('remaining',64);
    field('tlb',scenario.nullView?0:probe.mode===2?view:tlb);
    field('code_begin',0x70000000);field('code_end',0x70001000);
    helperTrace=[];
}
const scenarios=[{name:'valid'},{name:'read-only',permission:1},{name:'write-only',permission:2},
    {name:'denied',permission:0},{name:'null-view',nullView:true},{name:'zero-host',zeroHost:true},
    {name:'endian',endian:true},{name:'unaligned',address:0x8001},{name:'cross-page',address:0x8ffc},
    {name:'page-zero',address:0},{name:'arena',address:0x408040,arena:true},
    {name:'arena-cross-page',address:0x408ffc,arena:true}];
const rows=[];let reductions=0,unchanged=0;
for(let i=0;i<before.probes.length;i++) {
    const a=before.probes[i],b=after.probes[i];assert.equal(a.name,b.name);assert.equal(a.mode,b.mode);
    const raw=[a,b].map(p=>Buffer.from(p.wasm,'base64'));
    const instances=raw.map(blob=>new WebAssembly.Instance(counted(blob),{env}));
    let improved=0,comparisons=0;
    for(const scenario of scenarios)for(const budget of [0,1,3,16])for(const z of [0,1]) {
        const results=instances.map((instance,variant)=>{
            init(variant?b:a,scenario,budget,z);instance.exports.steps.value=0n;
            const count=instance.exports.f_4096(state);
            const hash=crypto.createHash('sha256');
            hash.update(bytes.subarray(state,state+1024));
            for(const guest of mapped)hash.update(bytes.subarray(physical(guest),physical(guest)+4096));
            return {steps:Number(instance.exports.steps.value),count,hash:hash.digest('hex'),helpers:helperTrace};
        });
        const [old,current]=results,label=`${a.name} mode=${a.mode} ${scenario.name} budget=${budget} z=${z}`;
        assert.equal(current.count,old.count,label+' guest count');assert.equal(current.hash,old.hash,label+' state/memory');
        assert.deepEqual(current.helpers,old.helpers,label+' helpers');
        assert(current.steps<=old.steps,`${label}: instructions increased ${old.steps} -> ${current.steps}`);
        if(current.steps<old.steps){improved++;reductions++;}else unchanged++;
        comparisons++;
        rows.push({name:a.name,mode:a.mode,scenario:scenario.name,budget,z,before:old.steps,after:current.steps,guest_instructions:current.count,helper_calls:current.helpers.length});
    }
    if(!raw[0].equals(raw[1]))assert(improved>0,`${a.name}: changed without an instruction-count reduction`);
    console.log('PASS',a.name,'mode',a.mode,improved,'reduced of',comparisons);
}
fs.writeFileSync(output,JSON.stringify({metric:'Executed WASM operations; block/loop/end/else markers and counter operations excluded; imported helper bodies excluded but call traces equal',reductions,unchanged,rows},null,2)+'\n');
console.log('PASS',reductions,'reduced,',unchanged,'unchanged, zero increased; state, memory, guest progress and helper traces match');
