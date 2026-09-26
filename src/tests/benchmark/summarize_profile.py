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
    weights = defaultdict(int)
    for node, delta in zip(profile.get('samples', []), profile.get('timeDeltas', [])):
        weights[nodes[node]['callFrame']['functionName'] or '(anonymous)'] += delta
    total = sum(weights.values())
    # A parked WASM worker can be sampled inside futex/condition waits rather
    # than Chrome's '(idle)' pseudo-frame. These samples are not busy CPU work.
    nonidle = total - sum(us for name, us in weights.items()
                          if name in ['(idle)', '(root)', '__timedwait_cp', 'emscripten_futex_wait'])
    summary['isolates'].append({'file': path.name, 'sampled_seconds': total / 1e6,
        'nonidle_seconds': nonidle / 1e6,
        'top_self': [{'function': name, 'seconds': us / 1e6, 'percent_all_samples': us * 100 / total if total else 0}
                     for name, us in sorted(weights.items(), key=lambda item: item[1], reverse=True)[:25]]})
summary['isolates'].sort(key=lambda item: item['nonidle_seconds'], reverse=True)
(a.directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps({**summary, 'isolates': summary['isolates'][:5]}, indent=2))
