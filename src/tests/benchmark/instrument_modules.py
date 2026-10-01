#!/usr/bin/env python3
"""Instrument an archived loader, never the served build. Exact input-byte match
is required before using an offline optimized replacement. No guest PC whitelist.
"""
import argparse,pathlib,shutil
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--replacements',type=pathlib.Path);p.add_argument('--metadata-only',action='store_true');a=p.parse_args()
a.output.mkdir()
for ext in ['js','wasm','data','html']:shutil.copy2(a.build/f'eka2l1.{ext}',a.output/f'eka2l1.{ext}')
s=(a.output/'eka2l1.js').read_text()
needle='var wasmModule=new WebAssembly.Module(wasmBytes);'
assert s.count(needle)==1
replacement='''var probeIndex=globalThis.ekaProbeIndex=(globalThis.ekaProbeIndex||0)+1;
var probeOriginal=new Uint8Array(wasmBytes);var probeReplaced=false;
PROBE_REPLACEMENT
var probeStart=performance.now();
var wasmModule=new WebAssembly.Module(wasmBytes);var probeCompileMs=performance.now()-probeStart;
var probeText='';for(var k=0;k<probeOriginal.length;k+=8192)probeText+=String.fromCharCode(...probeOriginal.subarray(k,k+8192));
(globalThis.ekaProbeModules||= []).push({index:probeIndex,compile_ms:probeCompileMs,bytes:wasmBytes.length,optimized:probeReplaced,exports:WebAssembly.Module.exports(wasmModule),base64:btoa(probeText)});'''
# Replacements embedded in the archived loader: no added network work during play.
if a.replacements:
 import base64,json
 entries=[]
 for f in sorted(a.replacements.glob('*module-*.opt.wasm')):
  old=f.with_name(f.name.replace('.opt.wasm','.wasm'))
  entries.append([base64.b64encode(old.read_bytes()).decode(),base64.b64encode(f.read_bytes()).decode()])
 prefix='var ekaProbeReplacements='+json.dumps(entries)+';'
 s=prefix+s
 hook='''var pair=ekaProbeReplacements.find(pair=>{if(!pair.decoded)pair.decoded=Uint8Array.from(atob(pair[0]),c=>c.charCodeAt(0));return pair.decoded.length===wasmBytes.length&&pair.decoded.every((v,i)=>v===wasmBytes[i]);});
if(pair){probeReplaced=true;wasmBytes=Uint8Array.from(atob(pair[1]),c=>c.charCodeAt(0));}'''
else:hook=''
if a.metadata_only:replacement=replacement.replace("var probeText='';for(var k=0;k<probeOriginal.length;k+=8192)probeText+=String.fromCharCode(...probeOriginal.subarray(k,k+8192));", "var probeText='';").replace('base64:btoa(probeText)','base64:null')
s=s.replace(needle,replacement.replace('PROBE_REPLACEMENT',hook))
(a.output/'eka2l1.js').write_text(s)
