// Uninstrumented microbenchmark: all dispatch and repetition stay inside WASM.
// Use --no-liftoff to isolate optimized native execution from tiering/startup.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const [input, output] = process.argv.slice(2);
if (!output) throw Error('thumb-static-native.mjs PROBES.log OUTPUT.json');
const raw=fs.readFileSync(input,'utf8');
const probes=input.endsWith('.json')?JSON.parse(raw).probes:
    raw.split('\n').filter(l=>l.startsWith('THUMB_REGIONS ')).map(l=>JSON.parse(l.slice(14)));
assert(probes.length);
const leb=n=>{const b=[];do{const x=n&127;n>>>=7;b.push(x|(n?128:0));}while(n);return b;};
const sleb=n=>{const b=[];for(;;){const x=n&127;n>>=7;const more=!((n===0&&!(x&64))||(n===-1&&(x&64)));b.push(x|(more?128:0));if(!more)return b;}};
const string=s=>[...leb(s.length),...Buffer.from(s)];
const state=0x1000,snapshot=0x8000,host=0x100000,view=0x6000,pages=0x200000,budget=100;
function runner(size) {
    const out=[0,97,115,109,1,0,0,0],section=(id,b)=>{out.push(id,...leb(b.length));for(const x of b)out.push(x);};
    section(1,[1,0x60,1,0x7f,1,0x7f]);
    section(2,[2,...string('env'),...string('memory'),2,3,...leb(256),...leb(65536),
        ...string('env'),...string('functions'),1,0x70,0,...leb(size)]);
    section(3,[1,0]);section(7,[1,...string('run'),0,0]);
    // locals: 0=repetitions, 1=progress, 2=dispatch index, 3=returned count,
    // 4=checksum. No JS call occurs inside either loop.
    const b=[1,4,0x7f],op=(...xs)=>b.push(...xs),c=n=>op(0x41,...sleb(n)),get=n=>op(0x20,n),set=n=>op(0x21,n);
    const load=offset=>{c(state);op(0x28,2,...leb(offset));};
    op(3,0x40); // repetition loop
    c(state);c(snapshot);c(1024);op(0xfc,10,0,0); // memory.copy
    c(0);set(1);
    op(2,0x40,3,0x40); // chain exit, dispatch loop
    get(1);c(budget);op(0x4f,0x0d,1); // progress >= budget
    load(60);c(4096);op(0x6b);c(1);op(0x76);set(2);
    get(2);c(size);op(0x4f,0x0d,1);
    c(state);c(budget);get(1);op(0x6b,0x36,2,...leb(848));
    c(state);get(2);op(0x11,0,0);set(3);
    get(1);get(3);op(0x6a);set(1);
    get(3);op(0x45,0x0d,1);
    load(840);load(844);op(0x72,0x45,0x0d,1);
    load(876);op(0x45);load(784);c(128);op(0x71,0x45,0x71,0x0d,1);
    op(0x0c,0,0x0b,0x0b);
    get(4);get(1);op(0x6a);set(4);
    get(0);c(1);op(0x6b,0x22,0,0x0d,0,0x0b);
    get(4);op(0x0b);section(10,[1,...leb(b.length),...b]);
    return new WebAssembly.Module(Uint8Array.from(out));
}
const cases=process.env.EKA_NATIVE_CASES?.split(',')??['nested','early_return','outside_branch','long_conditional','long_memory','dense_transfers'];
const rows=[];
for(const name of cases)for(const seed of [0,3]) {
    const pair=probes.filter(p=>p.name===name);assert.equal(pair.length,2,name);
    const versions=pair.map(p=>{
        const memory=new WebAssembly.Memory({initial:256,maximum:65536,shared:true}),words=new Uint32Array(memory.buffer);
        const unexpected=()=>{throw Error('Native mapped fixture entered a helper');};
        const instance=new WebAssembly.Instance(new WebAssembly.Module(Buffer.from(p.wasm,'base64')),{env:{memory,
            tlb_read32:unexpected,tlb_write32:unexpected,tlb_read16:unexpected,tlb_write16:unexpected,tlb_read8:unexpected,tlb_write8:unexpected}});
        const exports=Object.entries(instance.exports),table=new WebAssembly.Table({element:'anyfunc',initial:exports.length});
        for(const [key,fn]of exports)table.set((Number(key.slice(2))-4096)/2,fn);
        const run=new WebAssembly.Instance(runner(exports.length),{env:{memory,functions:table}}).exports.run;
        words[host/4+0x8000/4]=seed;words[state/4]=words[state/4+1]=seed;
        words[state/4+3]=words[state/4+5]=words[state/4+13]=0x8000;
        words[state/4+14]=0x2001;words[state/4+15]=0x1000;
        words[(state+784)/4]=48;words[(state+796)/4]=16;words[(state+828)/4]=1;
        words[(state+840)/4]=budget;words[(state+876)/4]=1;words[(state+852)/4]=view;
        words[view/4]=0x400000;words[view/4+1]=0x4000000;words[view/4+2]=host;
        words[view/4+3]=pages;words[view/4+4]=(host-0x400000)>>>0;
        for(const page of [7,8]){words[(pages+page*8)/4]=host+page*4096;words[(pages+page*8)/4+1]=host+page*4096;}
        words.set(words.subarray(state/4,state/4+256),snapshot/4);
        const progress=run(1),final=Array.from(words.subarray(state/4,state/4+256));final[848/4]=0;
        return {run,progress,final};
    });
    assert.equal(versions[0].progress,versions[1].progress,name);assert.deepEqual(versions[0].final,versions[1].final,name);
    for(const v of versions)v.run(20000);
    const cpu=()=>process.threadCpuUsage(),delta=(a,b)=>a.user+a.system-b.user-b.system;
    let iterations=100000;
    const begin=cpu();versions[0].run(iterations);const duration=delta(cpu(),begin);
    iterations=Math.max(100000,Math.min(5000000,Math.round(iterations*150000/Math.max(1,duration))));
    const observations=[];
    for(let round=0;round<4;++round)for(const variant of round%2?[1,0]:[0,1]) {
        const start=cpu(),checksum=versions[variant].run(iterations),us=delta(cpu(),start);
        assert.equal(checksum,(versions[variant].progress*iterations)|0);
        observations.push({round,variant,cpu_us:us,iterations});
    }
    const means=[0,1].map(v=>observations.filter(r=>r.variant===v).reduce((n,r)=>n+r.cpu_us,0)/4);
    const result={name,seed,guest_progress:versions[0].progress,mean_change_percent:100*(means[1]/means[0]-1),observations};
    rows.push(result);console.log(name,seed,result.mean_change_percent.toFixed(2)+'%');
}
fs.writeFileSync(output,JSON.stringify({v8:process.versions.v8,node:process.version,execArgv:process.execArgv,metric:'thread CPU microseconds; WASM dispatch; no counters or sampling',rows},null,2)+'\n');
