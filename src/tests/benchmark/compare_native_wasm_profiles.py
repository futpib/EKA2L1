#!/usr/bin/env python3
"""Compare self-sample attribution; scopes and generated inclusive time are separate."""
import argparse, collections, json, pathlib, re
p=argparse.ArgumentParser()
p.add_argument('--wasm', type=pathlib.Path, required=True)
p.add_argument('--native', type=pathlib.Path, required=True, help='symbolized native-pcs.summary.json')
p.add_argument('--output', type=pathlib.Path, required=True)
a=p.parse_args()
ws=json.loads((a.wasm/'summary.json').read_text())
isolate=ws['isolates'][0]
w=json.loads((a.wasm/isolate['file']).read_text())
nodes={x['id']:x['callFrame'] for x in w['nodes']}
groups=collections.Counter(); entries=collections.Counter()
for node,delta in zip(w['samples'],w['timeDeltas']):
 f=nodes[node];name=f['functionName'];url=f['url'];seconds=delta/1e6
 if url.startswith('wasm://'):
  category='generated guest code self'
  m=re.search(r'(?:_pc_|^f_)(\d+)',name)
  if m: entries[int(m[1])]+=seconds
 elif any(x in name for x in ['validated_code_cache','equal_code_bytes','lookup_compiled']):category='compiled lookup and validity self'
 elif 'InterpreterMainLoop' in name:category='outer CPU loop self (includes compiled dispatch)'
 elif name.startswith('aot_tlb_'):category='memory helper self'
 elif name in ['(idle)','(root)','__timedwait_cp','emscripten_futex_wait']:category='waiting or profiler boundary'
 else:category='other runtime self'
 groups[category]+=seconds
native=json.loads(a.native.read_text()); ng=collections.Counter(); ne=collections.Counter()
for row in native['top_self']:
 name=row['function'];seconds=row['estimated_wall_seconds'];m=re.match(r'JIT a32_[at]([0-9A-F]{8})_',name)
 if m:category='generated guest blocks self';ne[int(m[1],16)]+=seconds
 elif name.startswith('JIT '):category='named generated stubs self'
 elif name.startswith('anonymous executable'):category='unresolved generated code or dispatch'
 elif 'sem_' in name or 'futex' in name or 'pthread_cond' in name:category='synchronization self (may spin)'
 else:category='other runtime self'
 ng[category]+=seconds
pages=lambda entries:[{'guest_page':hex(pc),'seconds':seconds} for pc,seconds in sorted(collections.Counter({page:sum(v for pc,v in entries.items() if pc&~4095==page) for page in {pc&~4095 for pc in entries}}).items(),key=lambda x:x[1],reverse=True)[:20]]
result={'method':'Disjoint self samples. WASM time deltas include attach/stop boundaries; native seconds distribute observed sampling-run wall time by sample count. These are diagnostic times, not predicted savings. Guest-page grouping assigns WASM inlined callees to the region entry; native blocks have finer boundaries. Do not equate individual entries as identical workloads.',
 'wasm':{'directory':str(a.wasm),'isolate':isolate['file'],'measurement':ws['measurement'],'groups_seconds':dict(groups),'generated_inclusive':isolate['generated_code_inclusive'],'entry_pages':pages(entries)},
 'native':{'source':str(a.native),'metadata':native['metadata'],'coverage':native['nominal_sample_coverage'],'groups_seconds':dict(ng),'entry_pages':pages(ne)}}
a.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'wasm':dict(groups),'native':dict(ng)},indent=2))
