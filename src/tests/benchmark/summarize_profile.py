#!/usr/bin/env python3
"""Summarize Chrome CPU samples per isolate; do not sum overlapping threads/scopes."""
import argparse
from collections import defaultdict
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
a = p.parse_args()
report = json.loads((a.directory / 'report.json').read_text())
summary = {'measurement': report['measurement'], 'browser': report['browser'],
           'renderer': report['renderer'], 'isolates': []}
for path in sorted(a.directory.glob('*.cpuprofile')):
    profile = json.loads(path.read_text())
    nodes = {node['id']: node for node in profile['nodes']}
    parents = {child: node['id'] for node in profile['nodes'] for child in node.get('children', [])}
    # Generated AOT modules have wasm:// URLs; the linked emulator has an HTTP
    # eka2l1.wasm URL. Include callees of generated frames (e.g. memory imports).
    generated_nodes = set()
    generated_modules = set()
    for node in profile['nodes']:
        frame = node['callFrame']
        if frame['url'].startswith('wasm://') and (frame['functionName'].startswith('wasm-function[') or frame['functionName'].startswith(('f_', 'r_'))):
            generated_nodes.add(node['id'])
            generated_modules.add(frame['url'])
    inside_generated = {}
    for node in nodes:
        current = node
        path_to_root = []
        while current in nodes and current not in inside_generated:
            path_to_root.append(current)
            if current in generated_nodes:
                inside_generated[current] = True
                break
            current = parents.get(current)
        generated = inside_generated.get(current, False)
        for child in path_to_root: inside_generated[child] = generated
    generated_weight = 0
    weights = defaultdict(int)
    for node, delta in zip(profile.get('samples', []), profile.get('timeDeltas', [])):
        weights[nodes[node]['callFrame']['functionName'] or '(anonymous)'] += delta
        if inside_generated[node]: generated_weight += delta
    total = sum(weights.values())
    # A parked WASM worker can be sampled inside futex/condition waits rather
    # than Chrome's '(idle)' pseudo-frame. These samples are not busy CPU work.
    nonidle = total - sum(us for name, us in weights.items()
                          if name in ['(idle)', '(root)', '__timedwait_cp', 'emscripten_futex_wait'])
    summary['isolates'].append({'file': path.name, 'sampled_seconds': total / 1e6,
        'nonidle_seconds': nonidle / 1e6,
        'generated_code_inclusive': {'seconds': generated_weight / 1e6,
            'percent_all_samples': generated_weight * 100 / total if total else 0,
            'modules': len(generated_modules)},
        'top_self': [{'function': name, 'seconds': us / 1e6, 'percent_all_samples': us * 100 / total if total else 0}
                     for name, us in sorted(weights.items(), key=lambda item: item[1], reverse=True)[:25]]})
summary['isolates'].sort(key=lambda item: item['nonidle_seconds'], reverse=True)
(a.directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps({**summary, 'isolates': summary['isolates'][:5]}, indent=2))
