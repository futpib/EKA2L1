// Measure the installed Chromium's optimized lowering, independently of timing.
// Native inventories are not executed path counts or a cycle prediction.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
import puppeteer from '../wasm/node_modules/puppeteer/lib/esm/puppeteer/puppeteer.js';

export const sha256=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export function nativeCodeRange(code,address,disassembly) {
    const lines=disassembly.split('\n').filter(l=>/^\s*[0-9a-f]+:\s/.test(l));
    const tables=[];
    for(let i=0;i+1<lines.length;++i) {
        const load=/\blea\s+(r\w+),\[rip[^\]]*\]\s*#\s*0x([0-9a-f]+)/.exec(lines[i]);
        if(load&&new RegExp(`\\bjmp\\s+QWORD PTR \\[${load[1]}\\+\\w+\\*8\\]`).test(lines[i+1]))
            tables.push(parseInt(load[2],16));
    }
    if(!tables.length)return {executable_bytes:code.length,table_bytes:0,table_offsets:[]};
    const end=Math.min(...tables),boundaries=new Set(lines.map(l=>parseInt(l.trim(),16)));
    assert(end>0&&end<code.length&&(code.length-end)%8===0,'Unrecognized native data layout');
    for(const offset of tables)assert(offset>=end&&(offset-end)%8===0);
    // V8 appends absolute-target jump tables. Verify every excluded entry is
    // an instruction boundary before the pool; never silently ignore bad code.
    for(let offset=end;offset<code.length;offset+=8) {
        const target=Number(code.readBigUInt64LE(offset))-address;
        assert(target>=0&&target<end&&boundaries.has(target),'Unrecognized jump-table entry');
    }
    return {executable_bytes:end,table_bytes:code.length-end,table_offsets:tables};
}
export function nativeInventory(disassembly) {
    const instructions=disassembly.split('\n').flatMap(line=>{
        const match=/^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)\s*(\S.*)$/.exec(line);
        return match?[match[2]]:[];
    });
    assert(instructions.length,'Empty native disassembly');
    assert(!instructions.some(i=>i.includes('(bad)')||i.startsWith('.byte')),'Undecoded native instruction');
    const padding=i=>/^(?:(?:cs|data16)\s+)*nop\w*\b/.test(i);
    const memory=i=>!padding(i)&&!/^lea\b/.test(i)&&i.includes('[');
    return {
        instructions:instructions.filter(i=>!padding(i)).length,
        padding_instructions:instructions.filter(padding).length,
        memory_instructions:instructions.filter(memory).length,
        stack_memory_instructions:instructions.filter(i=>memory(i)&&/\[(?:r[bs]p)(?:[+\]-])/.test(i)).length,
        conditional_branches:instructions.filter(i=>/^j(?!mp)\w+\b/.test(i)).length,
        calls:instructions.filter(i=>/^call\b/.test(i)).length,
    };
}

export async function lowerWithChromium(modulePath,index,destination,executable) {
    assert(process.platform==='linux'&&process.arch==='x64','Native capture requires Linux x86-64');
    const bytes=fs.readFileSync(modulePath);
    assert(WebAssembly.validate(bytes),'Invalid source module');
    const flags=`--no-liftoff --no-wasm-lazy-compilation --print-wasm-code-function-index=${index}`;
    const browser=await puppeteer.launch({executablePath:executable,headless:true,
        args:['--single-process','--no-zygote','--enable-logging=stderr','--no-sandbox',
            '--disable-dev-shm-usage',`--js-flags=${flags}`]});
    let stdout='',stderr='';
    browser.process().stdout.on('data',chunk=>{stdout+=chunk.toString();});
    browser.process().stderr.on('data',chunk=>{stderr+=chunk.toString();});
    try {
        const page=await browser.newPage(),session=await page.createCDPSession();
        const version=await session.send('Browser.getVersion');
        await page.evaluate(async data=>{
            globalThis.costModule=await WebAssembly.compile(Uint8Array.from(atob(data),c=>c.charCodeAt(0)));
        },bytes.toString('base64'));
        for(let n=0;n<200&&!stdout.includes('--- End code ---');++n)
            await new Promise(resolve=>setTimeout(resolve,10));
        assert.equal((stdout.match(/--- WebAssembly code ---/g)??[]).length,1,'Expected exactly one native function');
        assert(stdout.includes(`index: ${index}\n`)&&stdout.includes('compiler: TurboFan'),'Wrong function or compilation tier');
        const match=/Instructions \(size = (\d+), (0x[0-9a-f]+)-(0x[0-9a-f]+)\)/.exec(stdout);
        assert(match,'Chromium did not expose native code');
        const code=Buffer.alloc(Number(match[1])),address=Number(match[2]);
        assert.equal(Number(match[3])-address,code.length);
        const fd=fs.openSync(`/proc/${browser.process().pid}/mem`,'r');
        try {assert.equal(fs.readSync(fd,code,0,code.length,address),code.length);}
        finally {fs.closeSync(fd);}
        fs.writeFileSync(destination+'.bin',code);
        const disassemble=stop=>execFileSync('objdump',['-D','-b','binary','-m','i386:x86-64','-Mintel',
            '--insn-width=16',...(stop?[`--stop-address=${stop}`]:[]),destination+'.bin'],
            {encoding:'utf8',maxBuffer:128*1024*1024});
        const raw=disassemble(),range=nativeCodeRange(code,address,raw);
        const assembly=range.table_bytes?disassemble(range.executable_bytes):raw;
        fs.writeFileSync(destination+'.asm',assembly);
        return {module:path.resolve(modulePath),module_sha256:sha256(bytes),function_index:index,
            flags,version,native_sha256:sha256(code),code_bytes:code.length,...range,...nativeInventory(assembly)};
    } finally {
        await browser.close();
        fs.writeFileSync(destination+'.stdout',stdout);fs.writeFileSync(destination+'.stderr',stderr);
    }
}

// Inclusive categories overlap: allocation can be inside translation. Never
// add them together. Raw self samples for generated functions are separate.
export function profileCost(directory) {
    const worker=JSON.parse(fs.readFileSync(path.join(directory,'capture-worker.json'),'utf8')).selected;
    const file=path.join(directory,worker+'.cpuprofile'),raw=fs.readFileSync(file),profile=JSON.parse(raw);
    assert.equal(profile.samples.length,profile.timeDeltas.length);
    const nodes=new Map(profile.nodes.map(n=>[n.id,n])),parents=new Map();
    for(const node of profile.nodes)for(const child of node.children??[]) {
        assert(!parents.has(child),'Profile node has multiple parents');parents.set(child,node.id);
    }
    const names=id=>nodes.get(id).callFrame.functionName;
    const categories={translation:name=>name.includes('translate_')||name.includes('state_local_cache::finish'),
        state_finalization:name=>name.includes('state_local_cache::finish'),
        allocation:name=>/malloc|(?:^|::)operator new|emscripten_builtin_free/.test(name)};
    const inclusive_us=Object.fromEntries(Object.keys(categories).map(k=>[k,0]));
    let rom_self_us=0,ram_self_us=0,private_self_us=0,sampled_us=0;
    for(let i=0;i<profile.samples.length;++i) {
        const id=profile.samples[i],dt=profile.timeDeltas[i],ancestors=[];
        assert(nodes.has(id)&&dt>=0);sampled_us+=dt;
        if(/^f_\d+$/.test(names(id)))rom_self_us+=dt;
        if(/^r_\d+_pc_\d+$/.test(names(id)))ram_self_us+=dt;
        if(/^f_\d+_(?:budget_short|memory_fallback)$/.test(names(id)))private_self_us+=dt;
        for(let n=id;n!==undefined;n=parents.get(n))ancestors.push(names(n));
        for(const [category,predicate]of Object.entries(categories))
            if(ancestors.some(predicate))inclusive_us[category]+=dt;
    }
    return {file,sha256:sha256(raw),sampled_us,guest_self_us:rom_self_us+ram_self_us+private_self_us,rom_self_us,ram_self_us,private_self_us,inclusive_us,
        scope:'Sampled time, not CPU counters. Inclusive categories overlap; captured-worker selection.'};
}
