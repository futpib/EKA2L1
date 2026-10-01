#!/usr/bin/env python3
"""Bounded Dynarmic IR -> WASM research emitter for unconditional integer kernels.
Rejects unknown ops. Valid aligned ordinary-memory fixtures only; NOT a backend.
No exceptions, code writes, conditionals, or partial-budget execution are modeled.
"""
import re,pathlib,subprocess,argparse
p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);p.add_argument('--wasm-as',required=True);a=p.parse_args()
regs={'sp':13,'lr':14,'pc':15,**{'r'+str(i):i for i in range(16)}}
for source in a.directory.glob('*.after.ir'):
 text=source.read_text();pc=int(source.name.split('.')[0]);cycles=int(re.search(r'cycles=(\d+)',text)[1]);out=[];locals=[];types={}
 def val(v,t='i32'):
  if v.startswith('%'):return '(local.get $v'+re.search(r'inst ([0-9a-f]+)',v)[1]+')'
  if v.startswith('#'):return f'({t}.const {int(v[1:],0)})'
  raise ValueError(v)
 for line in text.splitlines():
  m=re.match(r'\[([0-9a-f]+)\]\s+(?:noname = )?(\w+) (.*?) \(uses: \d+\)',line)
  if not m:continue
  ident,op,args=m.groups();v=args.split(', ');v=[x.strip() for x in v];typ='i32';expr=None
  if op=='GetRegister':expr=f'(i32.load offset={regs[v[0]]*4} (local.get $s))'
  elif op=='SetRegister':out.append(f'(i32.store offset={regs[v[0]]*4} (local.get $s) {val(v[1])})');continue
  elif op=='BXWritePC':
   out.extend([f'(i32.store offset=60 (local.get $s) (i32.and {val(v[0])} (i32.const -2)))',f'(i32.store offset=828 (local.get $s) (i32.and {val(v[0])} (i32.const 1)))']);continue
  elif op=='ReadMemory32':expr=f'(call $read (local.get $s) {val(v[1])})'
  elif op=='WriteMemory32':out.append(f'(call $write (local.get $s) {val(v[1])} {val(v[2])})');continue
  elif op in ('Add32','Sub32','Add64'):
   typ='i64' if op=='Add64' else 'i32'; assert v[2] == ('#1' if op=='Sub32' else '#0'),v
   expr=f'({typ}.{"sub" if op=="Sub32" else "add"} {val(v[0],typ)} {val(v[1],typ)})'
  elif op in ('Or32','And32','Mul64'):
   typ='i64' if op=='Mul64' else 'i32';expr=f'({typ}.{dict(Or32="or",And32="and",Mul64="mul")[op]} {val(v[0],typ)} {val(v[1],typ)})'
  elif op=='SignExtendWordToLong':typ='i64';expr=f'(i64.extend_i32_s {val(v[0])})'
  elif op=='Pack2x32To1x64':typ='i64';expr=f'(i64.or (i64.extend_i32_u {val(v[0])}) (i64.shl (i64.extend_i32_u {val(v[1])}) (i64.const 32)))'
  elif op=='LeastSignificantWord':expr=f'(i32.wrap_i64 {val(v[0],"i64")})'
  elif op=='MostSignificantWord':expr=f'(i32.wrap_i64 (i64.shr_u {val(v[0],"i64")} (i64.const 32)))'
  elif op in ('LogicalShiftRight32','LogicalShiftLeft32'):
   assert v[1].startswith('#') and 0<int(v[1][1:],0)<32
   expr=f'(i32.{"shr_u" if op=="LogicalShiftRight32" else "shl"} {val(v[0])} {val(v[1])})'
  else:raise ValueError(op)
  locals.append(f'(local $v{ident} {typ})');out.append(f'(local.set $v{ident} {expr})')
 if 'BXWritePC' not in text:out.append(f'(i32.store offset=60 (local.get $s) (i32.const {pc+cycles*4}))')
 helper='''(func $address (param $s i32) (param $a i32) (param $write i32) (result i32) (local $e i32)
 (if (i32.or (i32.and (local.get $a) (i32.const 3)) (i32.and (i32.load offset=784 (local.get $s)) (i32.const 512))) (then unreachable))
 (local.set $e (i32.add (i32.load offset=852 (local.get $s)) (i32.shl (i32.and (i32.shr_u (local.get $a) (i32.const 12)) (i32.const 511)) (i32.const 4))))
 (if (i32.ne (i32.load (i32.add (local.get $e) (local.get $write))) (i32.and (local.get $a) (i32.const -4096))) (then unreachable))
 (i32.add (i32.load offset=12 (local.get $e)) (i32.and (local.get $a) (i32.const 4095))))
 (func $read (param $s i32) (param $a i32) (result i32) (i32.load (call $address (local.get $s) (local.get $a) (i32.const 0))))
 (func $write (param $s i32) (param $a i32) (param $v i32) (local $h i32)
 (local.set $h (call $address (local.get $s) (local.get $a) (i32.const 4)))
 (if (i32.and (i32.lt_u (local.get $h) (i32.load offset=860 (local.get $s))) (i32.gt_u (i32.add (local.get $h) (i32.const 4)) (i32.load offset=856 (local.get $s)))) (then unreachable))
 (i32.store (local.get $h) (local.get $v)))'''
 wat=f'''(module (import "env" "memory" (memory 32 32768 shared)) {helper}
 (func (export "run") (param $s i32) (result i32) {''.join(locals)}
 (if (i32.lt_u (i32.load offset=848 (local.get $s)) (i32.const {cycles})) (then (return (i32.const 0))))
 {''.join(out)} (i32.const {cycles})))'''
 dst=source.with_name(str(pc)+'.ir.wat');dst.write_text(wat)
 subprocess.run([a.wasm_as,str(dst),'-o',str(dst.with_suffix('.wasm')),'--enable-threads'],check=True)
 print(pc,'cycles',cycles,'locals',len(locals),'bytes',dst.with_suffix('.wasm').stat().st_size)
