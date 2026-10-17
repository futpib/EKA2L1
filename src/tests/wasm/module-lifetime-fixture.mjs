import assert from 'node:assert/strict';
import fs from 'node:fs';
import puppeteer from 'puppeteer';

const [output, countArg = '2000'] = process.argv.slice(2);
if (!output || !/^\d+$/.test(countArg)) throw Error('Usage: node module-lifetime-fixture.mjs OUTPUT.json [COUNT]');
const count = Number(countArg);
const browser = await puppeteer.launch({executablePath:'/usr/bin/chromium',headless:true,
  args:['--no-sandbox','--disable-dev-shm-usage']});
const system = await browser.target().createCDPSession();
const page = await browser.newPage();
const client = await page.createCDPSession();
const memory = async () => {
  const processes = (await system.send('SystemInfo.getProcessInfo')).processInfo;
  const rows = processes.flatMap(process => {
    try {
      const rollup = fs.readFileSync(`/proc/${process.id}/smaps_rollup`,'utf8');
      return [{...process,pssKiB:Number(rollup.match(/^Pss:\s+(\d+)/m)?.[1]??0)}];
    } catch { return []; }
  });
  let partitionAllocKiB=0, v8ExecutableKiB=0, v8DataKiB=0;
  for (const renderer of rows.filter(row => row.type === 'renderer')) {
    let mapping='';
    for (const line of fs.readFileSync(`/proc/${renderer.id}/smaps`,'utf8').split('\n')) {
      if (/^[0-9a-f]+-[0-9a-f]+ /.test(line)) mapping=line;
      else if (line.startsWith('Pss:')) {
        const kib=Number(line.match(/\d+/)?.[0]??0);
        if (mapping.includes('[anon:partition_alloc]')) partitionAllocKiB+=kib;
        if (mapping.includes('[anon:v8]')) {
          if (mapping.split(/\s+/)[1].includes('x')) v8ExecutableKiB+=kib;
          else v8DataKiB+=kib;
        }
      }
    }
  }
  return {wholeBrowserPssKiB:rows.reduce((sum,row)=>sum+row.pssKiB,0),
    rendererPssKiB:rows.filter(row=>row.type==='renderer').reduce((sum,row)=>sum+row.pssKiB,0),
    partitionAllocKiB,v8ExecutableKiB,v8DataKiB,
    processPssKiB:rows.map(row=>({type:row.type,pssKiB:row.pssKiB}))};
};

try {
  const baseline = await memory();
  await page.evaluate(count => {
    const unsigned = value => {
      const bytes=[];
      do {let byte=value&127;value>>>=7;if(value)byte|=128;bytes.push(byte);} while(value);
      return bytes;
    };
    const signed = value => {
      const bytes=[];
      for (;;) {
        let byte=value&127;
        value>>=7;
        const done=(value===0 && !(byte&64)) || (value===-1 && !!(byte&64));
        bytes.push(done?byte:byte|128);
        if(done)return bytes;
      }
    };
    const section = (id,bytes) => [id,...unsigned(bytes.length),...bytes];
    const moduleBytes = value => {
      const body=[0,0x41,...signed(value),...Array(256).fill(0x01),0x0b];
      return new Uint8Array([0,97,115,109,1,0,0,0,
        ...section(1,[1,0x60,0,1,0x7f]),
        ...section(3,[1,0]),
        ...section(7,[1,1,102,0,0]),
        ...section(10,[1,...unsigned(body.length),...body])]);
    };
    const table = new WebAssembly.Table({element:'anyfunc',initial:count});
    const mirror=[];
    const lookup=new WeakMap();
    const oldInstances=[], oldModules=[];
    for(let i=0;i<count;i++) {
      const module=new WebAssembly.Module(moduleBytes(i));
      const instance=new WebAssembly.Instance(module);
      const func=instance.exports.f;
      func();
      table.set(i,func);mirror[i]=table.get(i);lookup.set(func,i);
      oldInstances.push(new WeakRef(instance));
      oldModules.push(new WeakRef(module));
    }
    globalThis.lifetimeFixture={table,mirror,lookup,oldInstances,oldModules,moduleBytes};
  },count);
  const installed = await memory();
  await page.evaluate(() => {
    const {table,mirror,moduleBytes}=globalThis.lifetimeFixture;
    const module=new WebAssembly.Module(moduleBytes(1000000));
    const replacement=new WebAssembly.Instance(module).exports.f;
    for(let i=0;i<table.length;i++) {table.set(i,replacement);mirror[i]=table.get(i);}
    globalThis.lifetimeFixture.replacement=replacement;
  });
  const replaced = await memory();
  for(let i=0;i<3;i++) {
    await client.send('HeapProfiler.collectGarbage');
    await new Promise(resolve=>setTimeout(resolve,100));
  }
  await new Promise(resolve=>setTimeout(resolve,1000));
  const collected = await memory();
  const reachable = await page.evaluate(() => ({
    instances:globalThis.lifetimeFixture.oldInstances.filter(ref=>ref.deref()).length,
    modules:globalThis.lifetimeFixture.oldModules.filter(ref=>ref.deref()).length,
    tableEntries:globalThis.lifetimeFixture.table.length}));
  const report={browser:await browser.version(),count,baseline,installed,replaced,collected,reachable};
  fs.writeFileSync(output,JSON.stringify(report,null,2));
  console.log(JSON.stringify(report,null,2));
  assert.equal(reachable.instances,0);
  assert.equal(reachable.modules,0);
} finally { await browser.close(); }
