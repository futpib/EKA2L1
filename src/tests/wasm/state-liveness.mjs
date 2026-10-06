// Execute both sides of compiler liveness fixtures with independently counted
// operations, callback-visible snapshots and deterministic callback mutation.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {counted,resetCost,readCost} from './wasm-step-counter.mjs';
import {CostComparisons} from './wasm-cost-model.mjs';
const [input,output]=process.argv.slice(2);
if(!output)throw Error('state-liveness.mjs PROBES.log OUTPUT.json');
const probes=fs.readFileSync(input,'utf8').split('\n').filter(l=>l.startsWith('STATE_PROBE ')).map(l=>JSON.parse(l.slice(12)));
const leb=n=>{const bytes=[];do{const b=n&127;n>>>=7;bytes.push(b|(n?128:0));}while(n);return bytes;};
const string=s=>[...leb(s.length),...Buffer.from(s)];
function module(body,locals) {
    const bytes=[0,97,115,109,1,0,0,0],section=(id,b)=>bytes.push(id,...leb(b.length),...b);
    section(1,[2,0x60,1,0x7f,1,0x7f,0x60,1,0x7f,0]);
    section(2,[2,...string('env'),...string('memory'),2,0,1,...string('env'),...string('helper'),0,1]);
    section(3,[1,0]);section(7,[1,...string('probe'),0,1]);
    const code=[1,...leb(locals),0x7f,...Buffer.from(body,'hex'),0x0b];
    section(10,[1,...leb(code.length),...code]);
    const raw=Uint8Array.from(bytes);assert(WebAssembly.validate(raw));return counted(raw,{breakdown:true});
}
const memory=new WebAssembly.Memory({initial:1}),words=new Uint32Array(memory.buffer),state=64;
let trace=[],mutate=false;
const env={memory,helper:p=>{
    assert.equal(p,state);trace.push([...words.slice(state/4,state/4+64)]);
    if(mutate){words[state/4]=(words[state/4]+19)>>>0;words[state/4+1]^=0xa55a;}
}};
const costs=new CostComparisons();let comparisons=0,reduced=0;
for(const probe of probes) {
    const instances=[probe.before,probe.after].map(body=>new WebAssembly.Instance(module(body,probe.locals),{env}));
    let hits=0;
    for(let seed=0;seed<32;++seed)for(let condition=0;condition<5;++condition)for(const mutation of [false,true]) {
        const results=instances.map(instance=>{
            for(let i=0;i<64;++i)words[state/4+i]=(Math.imul(seed+1,0x1234567)+i)>>>0;
            words[state/4+2]=condition;trace=[];mutate=mutation;resetCost(instance);
            const result=instance.exports.probe(state);
            return {result,state:[...words.slice(state/4,state/4+64)],trace,cost:readCost(instance)};
        });
        const [a,b]=results,context={seed,condition,mutation};
        assert.equal(a.result,b.result,probe.name);assert.deepEqual(a.state,b.state,probe.name);assert.deepEqual(a.trace,b.trace,probe.name);
        assert(b.cost.operations<=a.cost.operations,probe.name);
        for(const key of Object.keys(a.cost.classes))assert(b.cost.classes[key]<=a.cost.classes[key],probe.name+' '+key);
        costs.add(probe.name,a.cost,b.cost,context);++comparisons;hits+=b.cost.operations<a.cost.operations;
    }
    if(probe.before!==probe.after)assert(hits>0,probe.name+' changed without measured savings');
    reduced+=hits;
}
assert(probes.length>0);
fs.writeFileSync(output,JSON.stringify({comparisons,reduced,unchanged:comparisons-reduced,cost_classes:costs.fixtures,probes},null,2)+'\n');
console.log('PASS state liveness:',comparisons,'execution/state/callback comparisons,',reduced,'reduced; no operation class grows');
