import assert from 'node:assert/strict';
import {instruction, staticCost, emptyCost, CostComparisons} from './wasm-cost-model.mjs';
import {counted,readCost,resetCost} from './wasm-step-counter.mjs';
import {moduleCosts} from './wasm-module-cost.mjs';

assert.deepEqual(staticCost(Uint8Array.from([0x20,0,0x28,2,0,0x21,1])).classes,
    {local:2,constant:0,load:1,store:0,control:0,call:0,global:0,memory_size:0,arithmetic:0,division:0});
assert.equal(staticCost(Uint8Array.from([0x02,0x40,0x41,0x80,1,0x0b])).operations,1);
assert.equal(instruction(Uint8Array.from([0x42,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,1]),0).end,11);
assert.throws(()=>instruction(Uint8Array.from([0xfd]),0),/Unaccounted/);
assert.throws(()=>instruction(Uint8Array.from([0x41,0x80]),0),/Truncated/);
assert.throws(()=>instruction(Uint8Array.from([0x44,0]),0),/Truncated/);
assert.throws(()=>instruction(Uint8Array.from([0x0e,127]),0),/Truncated/);

// Two branch paths and a private callee: body totals must not be confused
// with the operations that actually execute. No imported helper body is counted.
const body=[0,0x20,0,0x04,0x7f,0x20,0,0x10,1,0x05,0x41,9,0x0b,0x0b];
const callee=[0,0x20,0,0x41,7,0x6a,0x0b];
const raw=Uint8Array.from([0,97,115,109,1,0,0,0,
    1,6,1,0x60,1,0x7f,1,0x7f,3,3,2,0,0,
    7,9,1,5,112,114,111,98,101,0,0,
    10,3+body.length+callee.length,2,body.length,...body,callee.length,...callee]);
const instance=new WebAssembly.Instance(counted(raw,{breakdown:true}));
for(const input of [0,1]) {
    resetCost(instance);
    assert.equal(instance.exports.probe(input),input?8:9);
    const cost=readCost(instance);
    assert.equal(cost.operations,input?7:3);
    assert.equal(cost.classes.control,1);
    assert.equal(cost.classes.call,input?1:0);
    assert.equal(cost.classes.arithmetic,input?1:0);
    assert.equal(cost.classes.local,input?3:1);
    assert.equal(cost.operations,Object.values(cost.classes).reduce((a,b)=>a+b,0));
}
const comparisons=new CostComparisons();
comparisons.add('local savings',{operations:4,classes:{...emptyCost(),local:4}},
    {operations:2,classes:{...emptyCost(),local:1,control:1}},{budget:2});
assert.equal(comparisons.fixtures['local savings'].operation_growth,0);
assert.equal(comparisons.fixtures['local savings'].class_growth.control,1);
assert.equal(comparisons.fixtures['local savings'].growth_examples.control.context.budget,2);
assert.throws(()=>comparisons.add('incomplete',{operations:0,classes:{}},{operations:0,classes:{}}),/incomplete/);

function relocated(extra,value=7) {
    const out=[0,97,115,109,1,0,0,0],section=(id,b)=>out.push(id,b.length,...b);
    section(1,[1,0x60,1,0x7f,1,0x7f]);
    const bodies=[[0,0x20,0,0x10,extra?2:1,0x0b],
        ...(extra?[[0,0x41,0,0x0b]]:[]),[0,0x20,0,0x41,value,0x6a,0x0b]];
    section(3,[bodies.length,...bodies.map(()=>0)]);
    section(7,[1,6,...Buffer.from('f_4096'),0,0]);
    section(10,[bodies.length,...bodies.flatMap(b=>[b.length,...b])]);
    const bytes=Uint8Array.from(out);assert(WebAssembly.validate(bytes));return moduleCosts(bytes).f_4096;
}
const original=relocated(false),moved=relocated(true),changed=relocated(false,8);
assert.notEqual(original.raw_sha256,moved.raw_sha256);
assert.equal(original.normalized_sha256,moved.normalized_sha256);
assert.notEqual(original.normalized_sha256,changed.normalized_sha256);
assert.equal(original.operations,2);
console.log('PASS shared WASM cost specification, decoding and executed class counters');
