#!/usr/bin/env python3
"""Run archived native/WASM fault probes with verified explicit compiler policy."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('archive', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--unsafe-code',type=int,choices=(0,1,2,3),default=3)
parser.add_argument('--leaf-features',type=int,choices=range(256))
parser.add_argument('--literal-pc-veneers',action='store_true',help='Include runtime literal LDR-PC veneer fixture (requires a matching archive)')
parser.add_argument('--tail-prefixes',action='store_true',help='Include register-prefix tail-branch fixture (requires an archive containing it)')
parser.add_argument('--exit-census',type=int,choices=(0,1))
parser.add_argument('--predicated-leaves', type=int, choices=(0,1))
parser.add_argument('--execution-limits', help='Explicit window,leaf,sites,runner configuration')
parser.add_argument('--ir-policy', type=int, choices=range(19), required=True)
parser.add_argument('--long', action='store_true', help='Include 128-instruction coverage fixture (policies 13/14/15)')
parser.add_argument('--code-write-protect', type=int, choices=(0,1))
parser.add_argument('--code-lookup', type=int, choices=(0,1))
parser.add_argument('--code-compare', type=int, choices=(0,1,2,3,4))
parser.add_argument('--tlb-hash', type=int, choices=(0,1))
a = parser.parse_args()
if a.long and a.ir_policy not in (13,14,15,16):
    parser.error('--long requires policy 13, 14, 15 or 16')
a.output.mkdir()
root = Path(__file__).resolve().parents[3]
archive = a.archive.resolve()
cases = [('ir-conditions',5376),('ir-calls',96),('ir-calls-short',96),('ir-flags',480),
    ('invariant-write-remap',64),('invariant-remap',64),('ir-segments',672),
    ('ir-memory',480),('ir-memory-chain',48),('ir-wide',480),('ir-addressing',672),
    ('ir-short',480),('ir-recipes',480),('extended',672),('deferred',672),
    ('entry-budget',672),('entry-budget-deferred',672),('read-spans',48),
    ('wide-snapshots',672),('region-spans',48),('region-spans-interpreter',48),
    ('entry-budget-interpreter',672),('region-block-spans',96)]
if a.leaf_features is not None:
    cases.insert(0,('branch-veneers',5376))
    cases.insert(0,('expanded-calls',5376))
    cases.insert(0,('prefix-calls',5376))
    cases.insert(0,('preserve-inner',5376))
if a.predicated_leaves is not None:
    cases.insert(0,('predicated-calls',5376))
if a.literal_pc_veneers:
    if a.leaf_features is None: parser.error('--literal-pc-veneers requires --leaf-features')
    cases.insert(0,('literal-pc-veneers',5376))
if a.tail_prefixes:
    if a.leaf_features is None: parser.error('--tail-prefixes requires --leaf-features')
    cases.insert(0,('tail-prefixes',5376))
if a.long:
    cases.insert(0,('ir-long',96))
probe_compare = [] if a.code_compare is None else [f'--code-compare={a.code_compare}']
compare_args = [] if a.code_compare is None else ['--code-compare',str(a.code_compare)]
if a.code_lookup is not None:
    probe_compare.insert(0,f'--code-lookup={a.code_lookup}')
    compare_args += ['--code-lookup',str(a.code_lookup)]
if a.code_write_protect is not None:
    probe_compare.insert(0,f'--code-write-protect={a.code_write_protect}')
    compare_args += ['--code-write-protect',str(a.code_write_protect)]
if a.tlb_hash is not None:
    probe_compare += [f'--tlb-hash={a.tlb_hash}']
    compare_args += ['--tlb-hash',str(a.tlb_hash)]
if a.execution_limits is not None:
    probe_compare += [f'--execution-limits={a.execution_limits}']
    compare_args += ['--execution-limits',a.execution_limits]
if a.predicated_leaves is not None:
    probe_compare += [f'--predicated-leaves={a.predicated_leaves}']
    compare_args += ['--predicated-leaves',str(a.predicated_leaves)]
if a.exit_census is not None:
    probe_compare += [f'--exit-census={a.exit_census}']
    compare_args += ['--exit-census',str(a.exit_census)]
if a.leaf_features is not None:
    probe_compare += [f'--leaf-features={a.leaf_features}']
    compare_args += ['--leaf-features',str(a.leaf_features)]
if a.unsafe_code is not None:
    probe_compare += [f'--unsafe-code={a.unsafe_code}']
    compare_args += ['--unsafe-code',str(a.unsafe_code)]
results = []
for name, count in cases:
    for kind, command in [('native',[str(archive/'tests/eka_cpu_fault_native')]),
                          ('wasm',['node',str(archive/'tests/eka_cpu_fault_wasm.js')])]:
        with (a.output/f'{name}-{kind}.log').open('w') as log:
            subprocess.run(command+['--'+name,f'--ir-policy={a.ir_policy}']+probe_compare,
                           stdout=log,stderr=subprocess.STDOUT,check=True,cwd=root)
    result = a.output/f'{name}.json'
    subprocess.run(['python3',str(Path(__file__).with_name('compare_cpu_faults.py')),
                    str(a.output/f'{name}-native.log'),str(a.output/f'{name}-wasm.log'),str(result),
                    '--cases',str(count),'--ir-policy',str(a.ir_policy),'--require-equal']+compare_args,
                   check=True,stdout=subprocess.DEVNULL)
    results.append(json.loads(result.read_text()))
    (a.output/'summary.json').write_text(json.dumps(dict(unsafe_code=a.unsafe_code,literal_pc_veneers=a.literal_pc_veneers,tail_prefixes=a.tail_prefixes,leaf_features=a.leaf_features,exit_census=a.exit_census,predicated_leaves=a.predicated_leaves,execution_limits=a.execution_limits,code_write_protect=a.code_write_protect,code_lookup=a.code_lookup, ir_policy=a.ir_policy, tlb_hash=a.tlb_hash, code_compare=a.code_compare,
        completed_cases=sum(x['cases'] for x in results),results=results),indent=2)+'\n')
    print(name,'PASS',count,'policy',a.ir_policy,flush=True)
