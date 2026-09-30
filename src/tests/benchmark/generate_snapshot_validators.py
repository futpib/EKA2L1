#!/usr/bin/env python3
"""Offline exact-validator discriminator; synthetic snapshots, no CPU integration."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('assembler',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir()
sizes=list(range(33))+[47,48,49,63,64,65,95,96,97,127,128,129,191,192,193,228,255,256,257,511,512,513]
manifest={'assembler_sha256':hashlib.sha256(a.assembler.read_bytes()).hexdigest(),'cases':[]}
def vec(data):return '(v128.const i32x4 '+' '.join(str(int.from_bytes(data[i:i+4],'little')) for i in range(0,16,4))+')'
def generate(data,constant):
 n=len(data);stmts=[];offset=0
 def diff(i,size):
  if size==16:
   left=f'(v128.load offset={i} (local.get $a))';right=vec(data[i:i+16]) if constant else f'(v128.load offset={i} (local.get $b))'
   return f'(v128.xor {left} {right})'
  ty='i64' if size==8 else 'i32';op={8:'i64.load',4:'i32.load',2:'i32.load16_u',1:'i32.load8_u'}[size]
  left=f'({op} offset={i} (local.get $a))';right=f'({ty}.const {int.from_bytes(data[i:i+size],"little")})' if constant else f'({op} offset={i} (local.get $b))'
  return f'({ty}.ne {left} {right})'
 def reject(expr):stmts.append(f'(if {expr} (then (return (i32.const 0))))')
 while n-offset>=64:
  d=[diff(offset+i*16,16) for i in range(4)];reject(f'(v128.any_true (v128.or (v128.or {d[0]} {d[1]}) (v128.or {d[2]} {d[3]})))');offset+=64
 while n-offset>=16:reject(f'(v128.any_true {diff(offset,16)})');offset+=16
 for width in [8,4,2,1]:
  if n-offset>=width:reject(diff(offset,width));offset+=width
 return '\n'.join(stmts)+'\n(i32.const 1)'
for n in sizes:
 data=bytes(((i*73+(i>>2)*19+117)&255) for i in range(n));wat=['(module','(import "env" "memory" (memory 256 32768 shared))','(import "env" "compare" (func $production (param i32 i32 i32) (result i32)))']
 for name in ['generic','constant']:
  wat.append(f'(func ${name} (export "{name}") (param $a i32) (param $b i32) (param $n i32) (result i32) {generate(data,name=="constant")})')
 for name in ['production','generic','constant']:
  # The unknown noise address can alias either input. The same store in each
  # wrapper prevents invariant reads from being hoisted out of the hot loop.
  wat.append(f'''(func (export "loop_{name}") (param $a i32) (param $b i32) (param $noise i32) (param $count i32) (result i32)
   (local $i i32) (local $sum i32)
   (block $done (loop $again
    (br_if $done (i32.ge_u (local.get $i) (local.get $count)))
    (i32.store8 (local.get $noise) (local.get $i))
    (local.set $sum (i32.add (local.get $sum) (call ${name} (local.get $a) (local.get $b) (i32.const {n}))))
    (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $again)))
   (local.get $sum))''')
 wat.append(')');src=a.output/f'{n}.wat';out=a.output/f'{n}.wasm';src.write_text('\n'.join(wat));start=time.perf_counter();subprocess.run([str(a.assembler),str(src),'-o',str(out),'--enable-simd','--enable-threads'],check=True,capture_output=True)
 manifest['cases'].append({'size':n,'expected':data.hex(),'wasm_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'bytes':out.stat().st_size,'assembly_seconds':time.perf_counter()-start})
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
