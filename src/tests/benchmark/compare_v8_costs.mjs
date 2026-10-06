// Calibrate the WASM inventory against actual Chromium native code and account
// separately for translation work. Do not run alongside timing campaigns.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {moduleCosts} from '../wasm/wasm-module-cost.mjs';
import {lowerWithChromium,profileCost} from './v8-native-cost.mjs';

const [comparison,output,limit='6']=process.argv.slice(2);
if(!output)throw Error('compare_v8_costs.mjs MODULE_COMPARISON.json OUTPUT_DIRECTORY [TOP_N]');
assert(Number.isSafeInteger(Number(limit))&&Number(limit)>0);
const input=JSON.parse(fs.readFileSync(comparison,'utf8'));
fs.mkdirSync(output);
// Line-buffer Chromium output so native memory can be read before browser exit.
// Positional shell arguments preserve executable paths without interpolation.
const wrapper=path.resolve(output,'chromium.sh');
fs.writeFileSync(wrapper,'#!/bin/sh\nexec stdbuf -oL -eL "${EKA_COST_CHROMIUM:-/usr/lib/chromium/chromium}" "$@"\n',{mode:0o700});
const rows=[];
const result={scope:'Static native inventories of sampled changed functions, not executed path counts. Compiler time and native execution require separate validation.',
    selection:'Largest summed baseline/candidate self sample times; changed common exports only.',
    before_profile:profileCost(input.before.directory),after_profile:profileCost(input.after.directory),rows};
for(const row of [...input.changed].sort((a,b)=>b.before_self_us+b.after_self_us-a.before_self_us-a.after_self_us).slice(0,Number(limit))) {
    const native={};
    for(const variant of ['before','after']) {
        const file=path.join(input[variant].directory,row[variant].module),wasm=moduleCosts(fs.readFileSync(file),{includeRam:true})[row.name];
        assert.equal(wasm.normalized_sha256,row[variant].normalized_sha256,'Module comparison is stale');
        native[variant]=await lowerWithChromium(file,wasm.function_index,path.resolve(output,`${row.name}-${variant}`),wrapper);
    }
    assert.equal(native.before.version.jsVersion,native.after.version.jsVersion,'V8 changed between measurements');
    const delta=Object.fromEntries(['instructions','code_bytes','memory_instructions','stack_memory_instructions',
        'conditional_branches','calls'].map(k=>[k,native.after[k]-native.before[k]]));
    rows.push({name:row.name,guest_pc:row.guest_pc,thumb:row.thumb,before_self_us:row.before_self_us,after_self_us:row.after_self_us,
        wasm:{before:row.before,after:row.after},native,delta});
    fs.writeFileSync(path.join(output,'costs.json'),JSON.stringify(result,null,2)+'\n');
    console.log(row.name,JSON.stringify(delta));
}
