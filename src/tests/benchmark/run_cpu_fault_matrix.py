#!/usr/bin/env python3
"""Run archived native/WASM fault probes with verified explicit compiler policy."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('archive', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--ir-policy', type=int, choices=range(15), required=True)
parser.add_argument('--long', action='store_true', help='Include 128-instruction coverage fixture (policies 13/14)')
a = parser.parse_args()
if a.long and a.ir_policy not in (13,14):
    parser.error('--long requires policy 13 or 14')
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
if a.long:
    cases.insert(0,('ir-long',96))
results = []
for name, count in cases:
    for kind, command in [('native',[str(archive/'tests/eka_cpu_fault_native')]),
                          ('wasm',['node',str(archive/'tests/eka_cpu_fault_wasm.js')])]:
        with (a.output/f'{name}-{kind}.log').open('w') as log:
            subprocess.run(command+['--'+name,f'--ir-policy={a.ir_policy}'],
                           stdout=log,stderr=subprocess.STDOUT,check=True,cwd=root)
    result = a.output/f'{name}.json'
    subprocess.run(['python3',str(Path(__file__).with_name('compare_cpu_faults.py')),
                    str(a.output/f'{name}-native.log'),str(a.output/f'{name}-wasm.log'),str(result),
                    '--cases',str(count),'--ir-policy',str(a.ir_policy),'--require-equal'],
                   check=True,stdout=subprocess.DEVNULL)
    results.append(json.loads(result.read_text()))
    (a.output/'summary.json').write_text(json.dumps(dict(ir_policy=a.ir_policy,
        completed_cases=sum(x['cases'] for x in results),results=results),indent=2)+'\n')
    print(name,'PASS',count,'policy',a.ir_policy,flush=True)
