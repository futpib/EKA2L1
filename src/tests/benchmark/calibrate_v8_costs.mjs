// Calibration controls: these raw-WASM savings should disappear in optimized
// V8. Keep them distinct from measured reductions in actual game translations.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {staticCost} from '../wasm/wasm-cost-model.mjs';
import {lowerWithChromium} from './v8-native-cost.mjs';
const [output]=process.argv.slice(2);
if(!output)throw Error('calibrate_v8_costs.mjs OUTPUT_DIRECTORY');
fs.mkdirSync(output);
const wrapper=path.resolve(output,'chromium.sh');
fs.writeFileSync(wrapper,'#!/bin/sh\nexec stdbuf -oL -eL "${EKA_COST_CHROMIUM:-/usr/lib/chromium/chromium}" "$@"\n',{mode:0o700});
const cases=[
    {name:'local_copy',before:[0x20,0,0x21,1,0x20,1],after:[0x20,0]},
    {name:'constant_fold',before:[0x41,7,0x41,9,0x6a],after:[0x41,16]},
    {name:'add_zero',before:[0x20,0,0x41,0,0x6a],after:[0x20,0]},
];
const rows=[];
for(const fixture of cases) {
    const variants={};
    for(const variant of ['before','after']) {
        const out=[0,97,115,109,1,0,0,0],section=(id,b)=>out.push(id,b.length,...b);
        section(1,[1,0x60,1,0x7f,1,0x7f]);section(3,[1,0]);
        section(7,[1,6,...Buffer.from('f_4096'),0,0]);
        const body=[1,1,0x7f,...fixture[variant],0x0b];section(10,[1,body.length,...body]);
        const bytes=Uint8Array.from(out),file=path.resolve(output,`${fixture.name}-${variant}.wasm`);
        assert(WebAssembly.validate(bytes));fs.writeFileSync(file,bytes);
        const fn=new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports.f_4096;
        for(const n of [0,1,-1,0x7fffffff,-0x80000000])assert.equal(fn(n),fixture.name==='constant_fold'?16:n);
        variants[variant]={wasm:staticCost(Uint8Array.from(fixture[variant])),
            native:await lowerWithChromium(file,0,path.resolve(output,`${fixture.name}-${variant}`),wrapper)};
    }
    const before=variants.before,after=variants.after;
    assert(after.wasm.operations<before.wasm.operations);
    assert.equal(before.native.version.jsVersion,after.native.version.jsVersion);
    for(const key of ['instructions','memory_instructions','stack_memory_instructions','conditional_branches','calls'])
        assert.equal(before.native[key],after.native[key],`${fixture.name}: update V8 calibration for ${key}`);
    rows.push({name:fixture.name,...variants});
    fs.writeFileSync(path.join(output,'calibration.json'),JSON.stringify({scope:'Raw operation savings with equal optimized native inventories; no cycle claim.',rows},null,2)+'\n');
    console.log(fixture.name,`${before.wasm.operations} -> ${after.wasm.operations} WASM, ${before.native.instructions} -> ${after.native.instructions} native`);
}
