// Test-only WASM instrumentation. Never used in CPU timing builds.
import assert from 'node:assert/strict';
import {instruction, emptyCost} from './wasm-cost-model.mjs';
const leb = n => { const out=[]; do { const b=n&127; n>>>=7; out.push(b|(n?128:0)); } while(n); return out; };
function reader(bytes, start=0) {
    return {bytes, pos:start, u() { let n=0, shift=0, b; do { b=this.bytes[this.pos++]; n|=(b&127)<<shift; shift+=7; } while(b&128); return n>>>0; },
        skipLeb() { while(this.bytes[this.pos++]&128); }};
}
export function counted(bytes, {breakdown=false}={}) {
    const r=reader(bytes,8), sections=[];
    while(r.pos<bytes.length) { const id=bytes[r.pos++], n=r.u(); sections.push([id,bytes.slice(r.pos,r.pos+n)]); r.pos+=n; }
    assert(!sections.some(([id])=>id===6),'Probe unexpectedly has globals');
    const out=[...bytes.slice(0,8)];
    const section=(id,data)=>{out.push(id,...leb(data.length));for(const byte of data)out.push(byte);};
    const metrics=['steps',...(breakdown?Object.keys(emptyCost()).map(c=>'cost_'+c):[])];
    const increment=index=>[0x23,...leb(index),0x42,1,0x7c,0x24,...leb(index)];
    for(let [id,data] of sections) {
        if(id===7) {
            section(6,[...leb(metrics.length),...metrics.flatMap(()=>[0x7e,1,0x42,0,0x0b])]);
            const e=reader(data), n=e.u();
            data=Uint8Array.from([...leb(n+metrics.length),...data.slice(e.pos),
                ...metrics.flatMap((name,index)=>[...leb(name.length),...Buffer.from(name),3,...leb(index)])]);
        }
        if(id===10) {
            const c=reader(data), functions=c.u(), code=[...leb(functions)];
            for(let f=0;f<functions;f++) {
                const size=c.u(), end=c.pos+size, start=c.pos, groups=c.u();
                for(let g=0;g<groups;g++){c.u();c.pos++;}
                const body=[...data.slice(start,c.pos)];
                while(c.pos<end) {
                    const begin=c.pos, op=instruction(data,c.pos);c.pos=op.end;
                    if(op.operations) {
                        body.push(...increment(0));
                        if(breakdown)body.push(...increment(metrics.indexOf('cost_'+op.category)));
                    }
                    body.push(...data.slice(begin,c.pos));
                }
                assert.equal(c.pos,end);code.push(...leb(body.length));for(const byte of body)code.push(byte);
            }
            data=Uint8Array.from(code);
        }
        section(id,data);
    }
    return new WebAssembly.Module(Uint8Array.from(out));
}
export function resetCost(instance) {
    instance.exports.steps.value=0n;
    for(const category of Object.keys(emptyCost()))
        if(instance.exports['cost_'+category])instance.exports['cost_'+category].value=0n;
}
export function readCost(instance) {
    const number=value=>{const n=Number(value);assert(Number.isSafeInteger(n),'Cost counter exceeds exact integer range');return n;};
    return {operations:number(instance.exports.steps.value),classes:Object.fromEntries(
        Object.keys(emptyCost()).filter(c=>instance.exports['cost_'+c]).map(c=>[c,number(instance.exports['cost_'+c].value)]))};
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
