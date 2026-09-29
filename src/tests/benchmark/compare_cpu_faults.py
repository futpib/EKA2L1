#!/usr/bin/env python3
"""Compare the complete outputs of cpu_fault_probe, including callback-visible state."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('native', type=Path)
p.add_argument('wasm', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--require-equal', action='store_true', help='Exit nonzero on any semantic mismatch')
p.add_argument('--cases', type=int, choices=(48,96,480,672), default=480, help='Expected complete fixture count; read spans has 48, block spans has 96, extended has 672')
a = p.parse_args()

def load(path):
    rows = [json.loads(line[6:]) for line in path.read_text().splitlines() if line.startswith('FAULT ')]
    if len(rows) != a.cases or [r['id'] for r in rows] != list(range(a.cases)):
        raise ValueError(f'{path}: incomplete or duplicated probe output')
    return rows

native, wasm = load(a.native), load(a.wasm)
differences = []
fields = collections.Counter()
categories = collections.Counter()
state_fields = ['regs', 'cpsr', 'count', 'memory_changes']
state_matches = 0
for n, w in zip(native, wasm):
    for key in ['id', 'opcode', 'policy', 'address', 'endian', 'tlb_readonly', 'partial']:
        assert n[key] == w[key], f'Mismatched fixtures at {n["id"]}'
    state_matches += all(n[k] == w[k] for k in state_fields)
    changed = [k for k in n if n[k] != w[k]]
    if not changed:
        continue
    fields.update(changed)
    category = ('readonly_store' if n['tlb_readonly'] and not (n['opcode'] & (1 << 20))
                else 'big_endian_tlb_read' if n['tlb_readonly'] and n['endian']
                else 'big_endian_halfword_store' if n['opcode'] == 0xe1c100b0 and n['endian']
                else 'other')
    categories[category] += 1
    differences.append({'id': n['id'], 'category': category, 'fields': changed, 'native': n, 'wasm': w})
result = {'cases': len(native), 'all_fields_match': len(native) - len(differences),
          'final_state_and_exact_memory_match': state_matches,
          'mismatched_fields': dict(fields), 'categories': dict(categories),
          'little_endian_unmapped_cases': sum(not r['endian'] and not r['tlb_readonly'] for r in native),
          'little_endian_unmapped_differences': sum(not d['native']['endian'] and not d['native']['tlb_readonly'] for d in differences),
          'inputs': {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in [a.native,a.wasm]},
          'differences': differences}
a.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k:v for k,v in result.items() if k != 'differences'}, indent=2))

if a.require_equal and differences:
    raise SystemExit(1)
