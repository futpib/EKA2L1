#!/usr/bin/env python3
"""Summarize exact DynCom handler counts and sampled guest-code attribution."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
a = p.parse_args()
guest = json.loads((a.directory / 'guest-profile.json').read_text())
run = json.loads((a.directory / 'report.json').read_text())
m = run['measurement']
assert guest['executed'] + m['aot_instructions'] == m['last_instructions'] - m['first_instructions']
assert guest['decoded'] == m['decoded_instructions']
assert guest['dropped_samples'] == [0, 0]
result = {'stride': guest['stride'], 'measurement_guest_seconds': (m['last_virtual_us'] - m['first_virtual_us']) / 1e6,
          'guest_instructions': m['last_instructions'] - m['first_instructions'], 'aot_instructions': m['aot_instructions'],
          'git_head': run['git_head'], 'wasm_sha256': run['wasm_sha256'], 'streams': {}}
for kind, name in enumerate(['executed', 'decoded']):
    rows = [r for r in guest['samples'] if r['kind'] == kind]
    samples = sum(r['count'] for r in rows)
    assert samples == guest[name] // guest['stride']
    modules, processes, pcs = Counter(), Counter(), Counter()
    module_types = defaultdict(Counter)
    for r in rows:
        modules[r['module']] += r['count']
        processes[r['process']] += r['count']
        pcs[(r['module'], r['pc'], r['thumb'], r['handler'], r['opcode'])] += r['count']
        module_types[r['module']][('thumb:' if r['thumb'] else 'arm:') + r['handler']] += r['count']
    types = [r for r in guest['types'] if r['kind'] == kind]
    assert sum(r['count'] for r in types) == guest[name]
    result['streams'][name] = {'exact_instructions': guest[name], 'samples': samples,
        'unmapped_opcode_samples': sum(r['count'] for r in rows if not r['mapped']),
        'modules': [{'module': n, 'samples': c, 'percent_samples': c * 100 / samples,
                     'sampled_types': dict(module_types[n].most_common())} for n, c in modules.most_common()],
        'processes': [{'process': n, 'samples': c, 'percent_samples': c * 100 / samples} for n, c in processes.most_common()],
        'exact_handler_types': sorted(types, key=lambda r: r['count'], reverse=True),
        'hot_pcs': [{'module': key[0], 'pc': f'0x{key[1]:08x}', 'thumb': key[2], 'handler': key[3],
                     'opcode': f'0x{key[4]:08x}', 'samples': c, 'percent_samples': c * 100 / samples}
                    for key, c in pcs.most_common(50)]}
(a.directory / 'guest-summary.json').write_text(json.dumps(result, indent=2) + '\n')
for name, s in result['streams'].items():
    print(name, 'exact instructions', s['exact_instructions'], 'PC samples', s['samples'])
    for r in s['modules'][:12]: print(' module', r['module'], round(r['percent_samples'], 3))
    for r in s['exact_handler_types'][:12]: print(' type', 'Thumb' if r['thumb'] else 'ARM', r['handler'], r['count'], round(r['count']*100/s['exact_instructions'], 3))
