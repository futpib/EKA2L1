#!/usr/bin/env python3
"""Bounded direct-call switch versus indirect calls, with real validation imports.

Ordinary-memory captured fixtures only. This is not a guest-chain implementation.
"""
import argparse,copy,hashlib,json,subprocess
from pathlib import Path
from module_layout_probe import parse,emit
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('kernels',type=Path);p.add_argument('output',type=Path);p.add_argument('--bin',type=Path,required=True)
a=p.parse_args();a.output.mkdir();manifest={'scope':__doc__,'kernels':[]}
for pc,cycles in [(1879455500,57),(1879129820,7)]:
 source=a.kernels/f'{pc}.wasm';wat=a.output/f'{pc}.source.wat'
 subprocess.run([str(a.bin/'wasm-dis'),str(source),'-o',str(wat)],check=True)
 tree=parse(wat.read_text());functions=[n for n in tree if isinstance(n,list) and n[0]=='func']
 if len(functions)!=1:raise ValueError('Expected one ordinary kernel')
 m=['module',*[n for n in tree[1:] if n[0] not in ('func','export')]]
 m.append(['import','"env"','"table"',['table','$dispatch','32','funcref']])
 m.append(parse('(import "env" "validate" (func $validate (param i32) (result i32)))'))
 for i in range(32):
  f=copy.deepcopy(functions[0]);f[1]=f'$kernel{i}';m.extend([f,['export',f'"kernel{i}"',['func',f[1]]]])
 switch='(br_table '+' '.join(f'$case{i}' for i in range(32))+' $done (local.get $selector))'
 for i in range(32):switch=f'(block $case{i} {switch}) (local.set $count (call $kernel{i} (local.get $state))) (br $dispatched)'
 calls={'direct':'(block $dispatched '+switch+')','indirect':'(local.set $count (call_indirect (param i32) (result i32) (local.get $state) (local.get $selector)))'}
 for name,call in calls.items():
  m.append(parse(f'''(func (export "{name}") (param $state i32) (param $n i32) (param $mask i32) (result i32)
   (local $sum i32) (local $selector i32) (local $count i32)
   (block $done (loop $again
    (br_if $done (i32.eqz (local.get $n)))
    (local.set $selector (i32.and (local.get $n) (local.get $mask)))
    (br_if $done (i32.eqz (call $validate (local.get $state))))
    {call}
    (local.set $sum (i32.add (local.get $sum) (local.get $count)))
    (br_if $done (i32.eqz (local.get $count)))
    (local.set $n (i32.sub (local.get $n) (i32.const 1))) (br $again))) (local.get $sum))'''))
 out=a.output/f'{pc}.wasm';wat=a.output/f'{pc}.wat';wat.write_text(emit(m)+'\n')
 subprocess.run([str(a.bin/'wasm-as'),str(wat),'--enable-threads','-o',str(out)],check=True)
 manifest['kernels'].append({'pc':pc,'cycles':cycles,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'module_sha256':hashlib.sha256(out.read_bytes()).hexdigest()})
(a.output/'native-fixtures.json').write_bytes((a.kernels/'native-fixtures.json').read_bytes())
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
