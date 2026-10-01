#!/usr/bin/env python3
"""Disjoint stack attribution for the real WASM compiled dispatcher.

Use the supplied diagnostic patch/Binaryen wrapper and --no-wasm-inlining for
stage separation. Do not describe these perturbed times as unmodified costs.
"""
import argparse
import collections
import json
import re
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('directory',type=Path)
a=p.parse_args()
profiles=[]
for f in a.directory.glob('worker*.cpuprofile'):
    j=json.loads(f.read_text())
    functions={n['id']:n['callFrame']['functionName'] for n in j['nodes']}
    weight=sum(delta for node,delta in zip(j.get('samples',[]),j.get('timeDeltas',[]))
               if re.match(r'^[rf]_\d',functions[node]))
    if weight: profiles.append((weight,f,j))
if not profiles:raise ValueError('No generated guest-region samples found')
_,f,j=max(profiles,key=lambda item:item[0])
nodes={n['id']:n for n in j['nodes']}
parents={c:n['id'] for n in j['nodes'] for c in n.get('children',[])}
groups=collections.Counter();names=collections.Counter();samples=collections.Counter()
for node,delta in zip(j['samples'],j['timeDeltas']):
    stack=[];i=node
    while i in nodes:
        stack.append(nodes[i]['callFrame']);i=parents.get(i)
    texts=[x['functionName'] for x in stack];name=texts[0];joined='\n'.join(texts)
    if any(re.match(r'^[rf]_\d', x['functionName']) for x in stack):category='generated_regions_and_callees'
    elif 'primary_bytes_equal' in joined:category='primary_byte_validation'
    elif 'dependencies_equal' in joined:category='dependency_byte_validation'
    elif 'equal_code_bytes' in joined or 'bytes_match' in joined:category='validation_other'
    elif 'code_mapping&' in joined and ('mmu_base' in joined or 'mem::' in joined):category='mapping_resolution'
    elif 'validated_code_cache::find' in joined:category='ram_cache_lookup_and_mapping_guards'
    elif 'registry::lookup' in joined:category='rom_registry_lookup'
    elif 'lookup_compiled' in joined:category='lookup_wrapper_and_guard_publication'
    elif 'execute_chain_impl' in joined:category='chain_prepare_call_return_and_budget_accounting'
    elif name in ['(idle)','(root)','__timedwait_cp','emscripten_futex_wait']:category='wait_or_profiler_boundary'
    elif 'InterpreterMainLoop' in name:category='outer_cpu_dispatch_self'
    else:category='other_runtime'
    groups[category]+=delta/1e6;samples[category]+=1;names[name]+=delta/1e6
report=json.loads((a.directory/'report.json').read_text())
result={'profile':f.name,'measurement':report['measurement'],
        'stage_boundaries_present':{name:any(name in n['callFrame']['functionName'] for n in j['nodes'])
          for name in ['primary_bytes_equal','dependencies_equal','bytes_match','execute_chain_impl']},
        'method':'Disjoint nearest-stage stack buckets, generated region and callees first. Timer instrumentation disabled. Diagnostic no-inline build and V8 flag perturb code: compare surrounding controls, do not extrapolate exact savings.',
        'sampled_seconds':sum(groups.values()),'groups_seconds':dict(groups),'sample_counts':dict(samples),
        'top_self':[{'name':k,'seconds':v} for k,v in names.most_common(40)]}
(a.directory/'dispatch-breakdown.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='top_self'},indent=2))
