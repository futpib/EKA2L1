// Test-only WASM instrumentation. Never used in CPU timing builds.
import assert from 'node:assert/strict';
const leb = n => { const out=[]; do { const b=n&127; n>>>=7; out.push(b|(n?128:0)); } while(n); return out; };
function reader(bytes, start=0) {
    return {bytes, pos:start, u() { let n=0, shift=0, b; do { b=this.bytes[this.pos++]; n|=(b&127)<<shift; shift+=7; } while(b&128); return n>>>0; },
        skipLeb() { while(this.bytes[this.pos++]&128); }};
}
export function counted(bytes) {
    const r=reader(bytes,8), sections=[];
    while(r.pos<bytes.length) { const id=bytes[r.pos++], n=r.u(); sections.push([id,bytes.slice(r.pos,r.pos+n)]); r.pos+=n; }
    assert(!sections.some(([id])=>id===6),'Probe unexpectedly has globals');
    const out=[...bytes.slice(0,8)];
    const section=(id,data)=>{out.push(id,...leb(data.length));for(const byte of data)out.push(byte);};
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
                assert.equal(c.pos,end);code.push(...leb(body.length));for(const byte of body)code.push(byte);
            }
            data=Uint8Array.from(code);
        }
        section(id,data);
    }
    return new WebAssembly.Module(Uint8Array.from(out));
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
