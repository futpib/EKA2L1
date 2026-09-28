#!/usr/bin/env python3
"""Symbolize guest-thread PC samples; self time only, no invented call stacks."""
import argparse, collections, json, pathlib, subprocess
p=argparse.ArgumentParser();p.add_argument('samples',type=pathlib.Path);a=p.parse_args()
meta=json.loads(pathlib.Path(str(a.samples)+'.json').read_text())
maps=[]
for line in pathlib.Path(str(a.samples)+'.maps').read_text().splitlines():
 parts=line.split(maxsplit=5);start,end=(int(x,16) for x in parts[0].split('-'))
 maps.append((start,end,parts[1],parts[5] if len(parts)>5 else ''))
rows=[];groups=collections.defaultdict(list)
for line in a.samples.read_text().splitlines()[1:]:
 pc,count,offset,obj,symbol=line.split('\t');row={'pc':pc,'count':int(count),'offset':offset,'object':obj,'symbol':symbol};rows.append(row)
 if obj!='<anonymous>' and pathlib.Path(obj).is_file():groups[obj].append(row)
for obj,items in groups.items():
 output=subprocess.check_output(['addr2line','-f','-C','-e',obj],input='\n'.join('0x'+r['offset'] for r in items)+'\n',text=True).splitlines()
 for r,fn,line in zip(items,output[::2],output[1::2]):
  r['function']=fn if fn!='??' else r['symbol'] or 'unresolved';r['source']=line
for r in rows:
 address=int(r['pc'],16);mapping=next((m for m in maps if m[0]<=address<m[1]),None)
 if r['object']=='<anonymous>':
  r['function']='anonymous executable (JIT code and dispatcher)' if mapping and 'x' in mapping[2] else 'unresolved anonymous'
  r['mapping']=mapping
 r.setdefault('function',r['symbol'] or 'unresolved')
functions=collections.Counter()
for r in rows:functions[r['function']]+=r['count']
n=sum(functions.values());result={'metadata':meta,'nominal_sample_coverage':n*.001/meta['wall_seconds'],
 'warning':'PC-only self samples. Anonymous executable code includes JIT dispatch, not just guest arithmetic. Estimated seconds weight observed wall time; missing/coalesced samples may bias attribution.',
 'top_self':[{'function':fn,'samples':c,'share':c/n,'estimated_wall_seconds':meta['wall_seconds']*c/n} for fn,c in functions.most_common()], 'samples':rows}
a.samples.with_suffix('.summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({**result,'samples':None,'top_self':result['top_self'][:25]},indent=2))
