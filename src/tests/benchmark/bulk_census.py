#!/usr/bin/env python3
"""Read-only structural census. Candidate-containing samples are upper bounds,
not safe bulk iterations: runtime permissions/aliasing/budgets are not proved.
Requires capstone and the existing Thumb matcher CLI built by --matcher-source.
"""
import argparse,collections,json,pathlib,re,subprocess
import capstone as cs
p=argparse.ArgumentParser();p.add_argument('run',type=pathlib.Path);p.add_argument('--matcher',default='/tmp/bulk-matcher');a=p.parse_args()
code={}
for space,pc,h in re.findall(r'CENSUS_CODE (\d+) (\d+) ([0-9a-f]+)',(a.run/'browser.log').read_text()):code[int(space),int(pc)]=bytes.fromhex(h)
rows=[]
for (space,pc),raw in code.items():
 thumb=bool(pc&1);base=pc&~1
 md=cs.Cs(cs.CS_ARCH_ARM,cs.CS_MODE_THUMB if thumb else cs.CS_MODE_ARM);md.detail=True
 ins={i.address:i for i in md.disasm(raw,base)}
 reachable=set();todo=[base]
 while todo:
  at=todo.pop()
  if at in reachable or at not in ins:continue
  reachable.add(at);i=ins[at];m=i.mnemonic
  if i.group(cs.CS_GRP_CALL):todo.append(at+i.size)
  elif i.group(cs.CS_GRP_JUMP):
   if i.operands and i.operands[-1].type==cs.arm.ARM_OP_IMM:todo.append(i.operands[-1].imm)
   if i.cc not in [cs.arm.ARM_CC_AL,cs.arm.ARM_CC_INVALID]:todo.append(at+i.size)
  elif m not in ['svc','udf'] and cs.arm.ARM_REG_PC not in i.regs_access()[1]:todo.append(at+i.size)
 loops=[]; other_memory_loops=[]
 for at in sorted(reachable):
  i=ins[at]
  if not i.group(cs.CS_GRP_JUMP) or not i.operands or i.operands[0].type!=cs.arm.ARM_OP_IMM:continue
  start=i.operands[0].imm
  body=[ins[x] for x in sorted(ins) if start<=x<at]
  if start not in reachable or not 2<=len(body)<=24:continue
  if any(x.group(cs.CS_GRP_JUMP) or x.group(cs.CS_GRP_CALL) for x in body):continue
  if any(x.mnemonic.startswith(('ldr','str','ldm','stm')) for x in body):
   other_memory_loops.append({'start':start,'instructions':len(body)+1,'assembly':[x.mnemonic+' '+x.op_str for x in body]+[i.mnemonic+' '+i.op_str]})
  if i.mnemonic!='bne':continue
  stores=[x for x in body if x.mnemonic.startswith('str')];loads=[x for x in body if x.mnemonic.startswith('ldr')]
  allowed={'mov','movs','mvn','mvns','add','adds','sub','subs','cmp','and','ands','orr','orrs','eor','eors','bic','bics','lsl','lsls','lsr','lsrs','asr','asrs','sxtb','sxth','uxtb','uxth'}
  if not 1<=len(stores)<=4 or len(loads)>1 or any(x.mnemonic not in allowed and not x.mnemonic.startswith(('ldr','str')) for x in body):continue
  loops.append({'start':start,'instructions':len(body)+1,'assembly':[x.mnemonic+' '+x.op_str for x in body]+[i.mnemonic+' '+i.op_str]})
 exact=[]
 if thumb:
  # Align the mock mapping just as ReadCode aligns its word reads.
  aligned=base&~3; b=b'\0'*(base-aligned)+raw
  out=subprocess.check_output([a.matcher],input=f'{aligned} {b.hex()}\n',text=True)
  exact=[int(x.split(':')[0]) for x in out.strip().split(',') if x and int(x.split(':')[0]) in reachable]
 rows.append({'asid':space,'pc':pc,'thumb':thumb,'loops':loops,'other_memory_loops':other_memory_loops,'thumb_matcher':exact})
index={(r['asid'],r['pc']):r for r in rows};g=json.loads((a.run/'guest-profile.json').read_text());tot=collections.Counter()
for e in g['edges']:
 weight=e['count']*e['length'];r=index.get((e['asid'],e['from']))
 tot['sampled_instructions']+=weight;tot['sampled_entries']+=e['count']
 if r:
  r['sampled_instructions']=r.get('sampled_instructions',0)+weight
  r['sampled_entries']=r.get('sampled_entries',0)+e['count']
  tot['snapshotted_instructions']+=weight
  if r['thumb']:tot['thumb_instructions']+=weight
  if r['loops']:tot['structural_candidate_region_instructions']+=weight
  if r['thumb_matcher']:tot['thumb_matcher_region_instructions']+=weight
result={'scope':'sampled compiled entries; candidate-region instruction attribution is an upper bound, not an exact eligible-loop count','totals':dict(tot),'compiled_blocks':g['compiled_blocks'],'fallback_instructions':g['executed'],'dropped_edges':g['dropped_edges'],'regions':sorted(rows,key=lambda r:r.get('sampled_instructions',0),reverse=True)}
(a.run/'bulk-census.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['totals'],indent=2))
print('candidate regions',sum(bool(r['loops']) for r in rows),'thumb matcher regions',sum(bool(r['thumb_matcher']) for r in rows))
for r in result['regions']:
 if r['loops'] or r['thumb_matcher']: print(hex(r['pc']),r.get('sampled_instructions'),r['loops'],r['thumb_matcher'])
