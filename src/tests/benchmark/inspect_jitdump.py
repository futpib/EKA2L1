#!/usr/bin/env python3
"""Extract V8's actual x86-64 code, independently from timing runs.
Static instruction counts are descriptive, not executed instruction counts.
"""
import argparse,pathlib,struct,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('dump',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir();b=a.dump.read_bytes();pos=struct.unpack_from('<I',b,8)[0];rows=[]
while pos+16<=len(b):
 kind,size,timestamp=struct.unpack_from('<IIQ',b,pos)
 if not size:raise ValueError('zero record')
 if pos+size>len(b):
  print(f'Ignoring incomplete trailing record at {pos}',file=__import__('sys').stderr);break
 if kind==0:
  end=b.index(0,pos+56);name=b[pos+56:end].decode();code_size=struct.unpack_from('<Q',b,pos+40)[0]
  if name.startswith(('JS:run-','JS:wasm-function[')):
   code=b[end+1:end+1+code_size];assert len(code)==code_size
   fn=a.output/f'{len(rows):02}.bin';fn.write_bytes(code)
   dis=subprocess.check_output(['objdump','-D','-b','binary','-m','i386:x86-64','-Mintel',str(fn)],text=True);fn.with_suffix('.asm').write_text(dis)
   ins=[m[1] for line in dis.splitlines() if (m:=re.match(r'\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*(\S.*)',line))]
   rows.append(dict(name=name,code_bytes=code_size,assembly=fn.with_suffix('.asm').name,instructions=len(ins),stack_operands=sum(bool(re.search(r'\[(?:r[bs]p)(?:[+\]-])',i))for i in ins),conditional_branches=sum(i.startswith('j') and not i.startswith('jmp') for i in ins),calls=sum(i.startswith('call') for i in ins)))
 pos+=size
(a.output/'summary.json').write_text(json.dumps(rows,indent=2));print(json.dumps([r for r in rows if 'turbofan' in r['name']],indent=2))
