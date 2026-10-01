#!/usr/bin/env python3
"""Summarize completed requests without treating notification latency as work."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics

p = argparse.ArgumentParser()
p.add_argument('run', type=Path)
a = p.parse_args()
d = json.loads((a.run / 'overlap.json').read_text())
assert not d['dropped'], 'Lifecycle records were dropped'
assert d['issued'] == len(d['completed']) + len(d['pending']) + d['replaced']
groups = defaultdict(list)
for row in d['completed']:
    phase = 0 if row['issue_us'] < 21000000 else 1 if row['issue_us'] < 78000000 else 2
    groups[phase, row['operation'], row['sync']].append(row)
summary = []
for (phase, op, sync), rows in sorted(groups.items()):
    pending = [r for r in rows if r['returned_pending']]
    summary.append(dict(phase=phase, operation=op, sync=sync, calls=len(rows),
                        completed_inline=len(rows)-len(pending), returned_pending=len(pending),
                        progressed_while_pending=sum(r['own_instructions'] > 0 for r in pending),
                        never_blocked_while_pending=sum(not r['blocked_while_pending'] for r in pending),
                        frames_while_pending=sum(r.get('presentations_while_pending',0)>0 for r in pending),
                        median_pending_guest_us=statistics.median(r['complete_us']-r['issue_us'] for r in pending) if pending else 0,
                        median_own_instructions=statistics.median(r['own_instructions'] for r in pending) if pending else 0,
                        median_before_first_wait=statistics.median(r['before_wait_own'] for r in pending if r['blocked_while_pending']) if any(r['blocked_while_pending'] for r in pending) else 0))
result = {'accounting': {k:d[k] for k in ['guest_us','instructions','issued','replaced','dropped','waits','blocked_waits','decompressor_pc','open_decompressions']},
          'threads': d['threads'], 'requests': summary, 'pending':d['pending'],
          'scopes':sorted(d['scopes'], key=lambda s:(s['phase'],-s['wall_ms']))}
(a.run / 'overlap-summary.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result['accounting']))
for phase in range(3):
    print('PHASE',phase)
    for row in summary:
        if row['phase']==phase and (row['returned_pending'] or row['calls']>10): print(json.dumps(row))
    for row in [r for r in result['scopes'] if r['phase']==phase][:12]:print(json.dumps(row))
