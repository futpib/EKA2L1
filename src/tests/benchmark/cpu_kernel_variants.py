#!/usr/bin/env python3
"""Offline diagnostic ablations for identity-mapped kernel fixtures, never gameplay.
Budget/flat variants deliberately weaken semantics and require full budgets and
valid ordinary memory. They quantify ceilings, not safe emulator optimizations.
"""
import argparse,copy,json,pathlib,re,subprocess
p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);p.add_argument('--bin',type=pathlib.Path,required=True);p.add_argument('--driver',type=pathlib.Path,required=True);a=p.parse_args()
subprocess.run([str(a.bin/'wasm-as'),str(a.driver),'--enable-threads','-o',str(a.directory/'driver.wasm')],check=True)
def parse(s):
 tokens=re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+',s);stack=[];root=None
 for t in tokens:
  if t=='(':
   x=[]
   if stack:stack[-1].append(x)
   stack.append(x)
  elif t==')':root=stack.pop()
  else:stack[-1].append(t)
 return root
def emit(x):return '('+' '.join(map(emit,x))+')' if isinstance(x,list) else x
def walk(x):
 if isinstance(x,list):
  yield x
  for c in x:yield from walk(c)
def has(x,term):return any(n==term for n in walk(x))
def assemble(tree,out,opt=False):
 wat=out.with_suffix('.wat');wat.write_text(emit(tree));subprocess.run([str(a.bin/'wasm-as'),str(wat),'--enable-threads','-o',str(out)],check=True)
 if opt:subprocess.run([str(a.bin/'wasm-opt'),str(out),'-O3','--enable-threads','-o',str(out)],check=True)
driver=parse(a.driver.read_text());loop=next(n for n in driver if isinstance(n,list) and n[0]=='func')
metadata={}
for arm in a.directory.glob('*.arm'):
 base=arm.with_suffix('.wasm');wat=arm.with_suffix('.wat');subprocess.run([str(a.bin/'wasm-dis'),str(base),'-o',str(wat)],check=True)
 tree=parse(wat.read_text());fields={}
 cycles=57 if arm.stem=='1879455500' else 7
 assemble(['module',['func',['export','\"run\"'],['param','i32'],['result','i32'],['i32.const',str(cycles)]]],arm.with_suffix('.empty.wasm'))
 for n in walk(tree):
  if n[:1]==['local.set'] and len(n)==3 and isinstance(n[2],list) and n[2][:1]==['i32.load']:
   for atom in n[2]:
    if isinstance(atom,str) and atom.startswith('offset='):fields[int(atom[7:])]=n[1]
 budget=fields[848];codefields=[fields[o] for o in [856,860] if o in fields];stats={}
 for kind in ['opt','budget','flat','both']:
  counts={'budgets':0,'page_guards':0,'span_guards':0,'code_write_guards':0}
  def transform(n):
   if not isinstance(n,list):return n
   if kind in ['budget','both'] and n==['i32.le_u',['local.get',budget],['local.get','$9']]:counts['budgets']+=1;return ['i32.const','0']
   if kind in ['flat','both'] and n[:1]==['if']:
    cond=n[1]
    if (has(cond,['local.get','$14']) or has(cond,['local.get','$16'])) and has(cond,['local.get','$10']):
     counts['page_guards']+=1;return ['local.set','$12',['local.get','$10']]
    if isinstance(cond,list) and cond[:2]==['local.tee','$13'] and has(n,['i32.const','4095']):
     addresses=[v[1] for v in walk(n) if v[:1]==['i32.and'] and len(v)==3 and v[2]==['i32.const','4095']]
     if not addresses:raise ValueError('span address not found')
     counts['span_guards']+=1;return ['local.set','$12',addresses[0]]
    if codefields and all(has(cond,['local.get',v]) for v in codefields) and has(cond,['local.get','$12']):
     counts['code_write_guards']+=1;return [n[0],['i32.const','0'],*[transform(c) for c in n[2:]]]
   return [transform(c) for c in n]
  changed=transform(copy.deepcopy(tree));assemble(changed,arm.with_suffix('.'+kind+'.wasm'),True);stats[kind]=counts
 same=copy.deepcopy(tree);same.append(copy.deepcopy(loop));assemble(same,arm.with_suffix('.same.wasm'))
 stats['sizes']={p.name:p.stat().st_size for p in a.directory.glob(arm.stem+'*.wasm')};metadata[arm.stem]=stats
(a.directory/'variants.json').write_text(json.dumps(metadata,indent=2)+'\n')
