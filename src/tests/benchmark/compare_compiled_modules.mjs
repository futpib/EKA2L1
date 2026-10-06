// Compare actual compiled ROM functions, including normalized private callees.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {moduleCosts} from '../wasm/wasm-module-cost.mjs';
const [beforeDir,afterDir,output,scope='rom']=process.argv.slice(2);
if(!output)throw Error('compare_compiled_modules.mjs BASELINE_PROFILE CANDIDATE_PROFILE OUTPUT.json [rom|all]');
assert(['rom','all'].includes(scope));
function load(directory) {
    const functions={},files=fs.readdirSync(directory).filter(n=>/-module-.*\.wasm$/.test(n));
    const workers=new Set(files.map(n=>n.slice(0,n.indexOf('-module-'))));
    assert.equal(workers.size,1,'Expected modules from exactly one captured worker');
    const worker=[...workers][0];
    for(const file of files) {
        const metadata=JSON.parse(fs.readFileSync(path.join(directory,file.replace(/wasm$/,'json')),'utf8'));
        if(!metadata.url.startsWith('wasm://'))continue;
        for(const [name,body]of Object.entries(moduleCosts(fs.readFileSync(path.join(directory,file)),{includeRam:scope==='all'}))) {
            if(functions[name])assert.equal(functions[name].normalized_sha256,body.normalized_sha256,'Different captured versions: '+name);
            functions[name]={...body,module:file};
        }
    }
    const profile=JSON.parse(fs.readFileSync(path.join(directory,'chrome-profile.json'),'utf8')).profiles
        .find(p=>p.file===worker+'.cpuprofile');
    assert(profile,'Captured worker has no CPU profile: '+worker);
    const weights={};for(const row of profile.frames)weights[row.name]=(weights[row.name]??0)+row.self_us;
    return {functions,weights,profile:profile.file,sampled_us:profile.sampled_us,modules:files.length};
}
const before=load(beforeDir),after=load(afterDir),names=Object.keys(before.functions).filter(n=>after.functions[n]);
const changed=names.filter(n=>before.functions[n].normalized_sha256!==after.functions[n].normalized_sha256)
    .map(name=>({name,guest_pc:'0x'+((Number(name.startsWith('r_')?name.split('_pc_')[1]:name.slice(2))&~1)>>>0).toString(16),
        thumb:name.startsWith('r_')?null:!!(Number(name.slice(2))%2),memory:name.startsWith('r_')?'ram':'rom',
        before:before.functions[name],after:after.functions[name],before_self_us:before.weights[name]??0,after_self_us:after.weights[name]??0}))
    .sort((a,b)=>b.before_self_us+b.after_self_us-a.before_self_us-a.after_self_us);
const result={scope:'Compiled inventory and sampled self time; not invocation counts. Private call indices normalized. Inventory operations are not executed path counts. RAM pairs require the same complete versioned export name; unmatched versions are not paired by address.',
    included_memory:scope,
    before:{directory:beforeDir,exports:Object.keys(before.functions).length,modules:before.modules,sampled_us:before.sampled_us},
    after:{directory:afterDir,exports:Object.keys(after.functions).length,modules:after.modules,sampled_us:after.sampled_us},
    common:names.length,unchanged:names.length-changed.length,changed,
    before_only:Object.keys(before.functions).filter(n=>!after.functions[n]),after_only:Object.keys(after.functions).filter(n=>!before.functions[n])};
fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');
console.log({common:result.common,changed:changed.length,thumb:changed.filter(r=>r.thumb).length,
    before_self_us:changed.reduce((n,r)=>n+r.before_self_us,0),after_self_us:changed.reduce((n,r)=>n+r.after_self_us,0)});
