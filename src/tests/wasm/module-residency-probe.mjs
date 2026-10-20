import fs from 'node:fs';
import path from 'node:path';

const [source, output] = process.argv.slice(2);
if (!source || !output || fs.existsSync(output))
  throw Error('Usage: node module-residency-probe.mjs BUILD NEW_OUTPUT');
fs.mkdirSync(output, {recursive: true});
for (const name of fs.readdirSync(source)) {
  const file = path.join(source, name);
  if (fs.statSync(file).isFile())
    fs.copyFileSync(file, path.join(output, name), fs.constants.COPYFILE_FICLONE);
}
const file = path.join(output, 'eka2l1.js');
let script = fs.readFileSync(file, 'utf8');
const replace = (needle, replacement) => {
  if (script.split(needle).length !== 2) throw Error('Generated JS changed: ' + needle);
  script = script.replace(needle, replacement);
};
let needle = 'var instance = new WebAssembly.Instance(wasmModule, importObj);';
replace(needle, needle + `
    var audit = globalThis.ekaModuleAudit ||= {modules: [], slots: new Map(), functions: 0, next: 5000};
    var record = {bytes: wasmBytes.byteLength, slots: 0, total: 0,
      instance: new WeakRef(instance), exports: []};
    var moduleId = audit.modules.length;
    audit.modules.push(record);
`);
needle = 'results.push(name + ":" + tableIdx);';
replace(needle, `
    var previous = audit.slots.get(tableIdx);
    if (previous !== undefined) --audit.modules[previous].slots;
    audit.slots.set(tableIdx, moduleId);
    record.exports.push(new WeakRef(func));
    ++record.slots; ++record.total; ++audit.functions;
` + needle);
needle = 'var resultStr = results.join(",");';
replace(needle, `
    if (audit.functions >= audit.next) {
      audit.next += 5000;
      var live = audit.modules.filter(r => r.slots);
      var dead = audit.modules.filter(r => !r.slots);
      console.log('[module-audit]', JSON.stringify({
        functions: audit.functions, modules: audit.modules.length,
        liveModules: live.length, deadModules: dead.length,
        liveBytes: live.reduce((n,r) => n+r.bytes,0), deadBytes: dead.reduce((n,r)=>n+r.bytes,0),
        liveSlots: live.reduce((n,r)=>n+r.slots,0), retiredInLive: live.reduce((n,r)=>n+r.total-r.slots,0),
        capacity: wasmMemory.buffer.byteLength
      }));
    }
` + needle);
needle = 'removeFunction(index);';
replace(needle, `
    var audit = globalThis.ekaModuleAudit;
    if (audit && audit.slots.has(index)) {
      --audit.modules[audit.slots.get(index)].slots;
      audit.slots.delete(index);
    }
` + needle);
fs.writeFileSync(file, script);

// Run the ordinary gameplay route first. Pause guest execution before checking
// collection, so continued game allocations cannot contaminate the GC interval.
const routes = JSON.parse(fs.readFileSync(new URL('./arena-growth-routes.json', import.meta.url)));
const collect = `(async()=>{
  await page.evaluate(()=>Module._eka2l1_route_pause_here());
  await page.waitForFunction(()=>Module._eka2l1_route_phase()===1);
  const before=await browserMemory();
  for(const worker of page.workers()){
    if(!await worker.evaluate(()=>typeof ekaModuleAudit==='object').catch(()=>false))continue;
    const snapshot=()=>{
      const live=ekaModuleAudit.modules.filter(m=>m.slots);
      const dead=ekaModuleAudit.modules.filter(m=>!m.slots);
      return {live:live.length,dead:dead.length,
        deadExports:dead.reduce((n,m)=>n+m.exports.length,0),
        deadReachable:dead.filter(m=>m.instance.deref()).length,
        deadExportsReachable:dead.reduce((n,m)=>n+m.exports.filter(f=>f.deref()).length,0),
        liveReachable:live.filter(m=>m.instance.deref()).length,
        liveBytes:live.reduce((n,m)=>n+m.bytes,0)};
    };
    const beforeAudit=await worker.evaluate(snapshot);
    await worker.client.send('HeapProfiler.collectGarbage');await wait(500);
    await worker.client.send('HeapProfiler.collectGarbage');await wait(500);
    const audit=await worker.evaluate(snapshot);
    const after=await browserMemory();
    fs.writeFileSync(out+'/module-collection.json',JSON.stringify({before,after,beforeAudit,audit},null,2));
    assert.equal(audit.deadReachable,0,'Retired module instances remain reachable');
    assert.equal(audit.deadExportsReachable,0,'Retired exported functions remain reachable');
    return audit;
  }
  throw Error('No instrumented CPU worker');
})()`;
for (const commands of Object.values(routes)) commands.push(collect);
fs.writeFileSync(path.join(output, 'collection-routes.json'), JSON.stringify(routes, null, 2));
console.log('Serve this private build and set EKA2L1_BENCHMARK_ROUTES=' +
  path.join(path.resolve(output), 'collection-routes.json'));
