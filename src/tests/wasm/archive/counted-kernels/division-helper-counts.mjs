// Historical instruction-count ABI only; see README.md in this directory.
// Offline exact-state comparison and executed operation counts. Not a timing build.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {counted,resetCost,readCost} from '../../wasm-step-counter.mjs';
import {CostComparisons} from '../../wasm-cost-model.mjs';
const [beforeFile,afterFile,output]=process.argv.slice(2);
if(!output)throw Error('division-helper-counts.mjs BASELINE_PROBES CANDIDATE_PROBES OUTPUT.json');
function parse(file) {
    const lines=fs.readFileSync(file,'utf8').split('\n');
    return {layout:JSON.parse(lines.find(x=>x.startsWith('MEMORY_LAYOUT ')).slice(14)),
        probe:JSON.parse(lines.find(x=>x.startsWith('MEMORY_PROBE ')).slice(13))};
}
const before=parse(beforeFile),after=parse(afterFile);assert.deepEqual(before.layout,after.layout);
assert.equal(before.probe.opcode,0);assert.equal(after.probe.opcode,1);
const S=before.layout,memory=new WebAssembly.Memory({initial:256,maximum:65536,shared:true});
const state=new Uint32Array(memory.buffer,65536,256),env={memory};
for(const name of ['tlb_read32','tlb_write32','tlb_read8','tlb_write8','tlb_read16','tlb_write16'])
    env[name]=()=>{throw Error('Unexpected memory callback');};
const instances=[before,after].map(x=>new WebAssembly.Instance(counted(Buffer.from(x.probe.wasm,'base64'),{breakdown:true}),{env}));
let random=0x247645ce;const next=()=>{random^=random<<13;random^=random>>>17;random^=random<<5;return random>>>0;};
const edge=[0,1,2,3,15,16,255,256,0x101e,0x40000000,0x7fffffff,0x80000000,0x80000001,0xffffefe2,0xfffffffe,0xffffffff];
const operands=[[0x40000000,0x101e],...edge.flatMap(n=>edge.map(d=>[n,d]))];
for(const q of [255,256,16383,16384,1048575,1048576,67108863,67108864])
    for(const signs of [0,1,2,3])operands.push([signs&1?-q>>>0:q,signs&2?0xffffffff:1]);
for(let i=0;i<128;++i)operands.push([next(),next()]);
const budgets=[0,1,2,4,8,24,25,34,39,62,63,64,86,87,88,114,115,116,134,135,136,137,256];
const scenarios=[{name:'ordinary'},{name:'irq',irq:0},{name:'masked-irq',irq:0,masked:true},{name:'exit',exit:1}];
const rows=[],costs=new CostComparisons();let checks=0,reduced=0,grown=0,unchanged=0;
for(let sample=0;sample<operands.length;++sample)for(const test of scenarios)for(const budget of budgets)for(const lr of [0x2000,0x2001]) {
    const [n,d]=operands[sample],initial=Array.from({length:256},next);
    initial[0]=n;initial[1]=d;initial[14]=lr;initial[15]=0x80191968;
    const flags=sample&15,cpsr=(flags<<28)|16|(test.masked?128:0);
    const field=(k,v)=>{initial[S[k]/4]=v;};field('mode',16);field('cpsr',cpsr);field('thumb',0);
    for(const [i,f]of ['n','z','c','v'].entries())field(f,(flags>>(3-i))&1);
    field('irq',test.irq??1);field('exit',test.exit??0);field('budget',budget);field('remaining',budget);initial[S.remaining/4+1]=0;
    const results=instances.map(instance=>{
        state.set(initial);resetCost(instance);const count=instance.exports.f_2149128552(65536);
        return {count,state:[...state],cost:readCost(instance)};
    });
    const [old,now]=results,label=JSON.stringify({n,d,budget,scenario:test.name,lr});
    assert.equal(now.count,old.count,label+' count');assert.deepEqual(now.state,old.state,label+' state');
    ++checks;if(now.cost.operations<old.cost.operations)++reduced;else if(now.cost.operations>old.cost.operations)++grown;else ++unchanged;
    if(sample<1&&lr===0x2000) {
        rows.push({n,d,scenario:test.name,budget,count:now.count,before:old.cost.operations,after:now.cost.operations});
        costs.add('complete-helper',old.cost,now.cost,{scenario:test.name,budget});
    }
}
fs.writeFileSync(output,JSON.stringify({checks,reduced,grown,unchanged,rows,cost_classes:costs.fixtures},null,2)+'\n');
console.log('PASS',checks,'exact state/count comparisons;',reduced,'reduced,',grown,'grown,',unchanged,'unchanged');
console.log(rows.find(x=>x.scenario==='ordinary'&&x.budget===256));
