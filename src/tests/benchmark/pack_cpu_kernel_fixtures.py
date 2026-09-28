#!/usr/bin/env python3
"""Pack native Step outputs as exact registers/flags and byte deltas from seeded RAM."""
import argparse,json,pathlib,struct
p=argparse.ArgumentParser();p.add_argument('native',type=pathlib.Path);p.add_argument('kernels',type=pathlib.Path);a=p.parse_args();out={}
for pc,cycles in [(1879455500,57),(1879129820,7)]:
 rows=[]
 for seed in range(1,17):
  initial=bytearray(0x20000);x=seed
  for address in range(0x10000,0x19000,4):
   x=(x*1664525+1013904223)&0xffffffff;struct.pack_into('<I',initial,address,x)
  for budget in range(cycles+1):
   data=(a.native/f'{pc}-{seed}-{budget}.bin').read_bytes()
   if len(data)!=76+0x20000:raise ValueError('Malformed native fixture')
   regs=list(struct.unpack_from('<16I',data));cpsr,ticks=struct.unpack_from('<IQ',data,64);memory=data[76:]
   if ticks!=budget:raise ValueError('Native Step instruction total differs')
   rows.append(dict(seed=seed,budget=budget,regs=regs,cpsr=cpsr,ticks=ticks,changes=[(i,memory[i]) for i in range(0x10000,0x20000) if initial[i]!=memory[i]]))
 out[str(pc)]=rows
out['prototype']=all((a.kernels/f'{pc}.precise.wasm').exists() for pc in [1879455500,1879129820])
(a.kernels/'native-fixtures.json').write_text(json.dumps(out)+'\n')
