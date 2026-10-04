#!/usr/bin/env python3
"""Summarize existing Chrome captures without treating samples as CPU timing."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path, help="Directory containing profile-GAME-0-MODE captures")
root = parser.parse_args().root
rows = []
for capture in sorted(root.glob('profile-*-0-*')):
    if not capture.is_dir():
        continue
    report = json.loads((capture / 'report.json').read_text())
    chrome = json.loads((capture / 'chrome-profile.json').read_text())
    guest = next(p for p in chrome['profiles'] if p['name'] == chrome['likely_guest_worker'])
    trace = json.loads((capture / 'trace.json').read_text())['traceEvents']
    starts = [e for e in trace if e['name'] == 'eka2l1:measurement-start' and e['ph'] == 'I']
    ends = [e for e in trace if e['name'] == 'eka2l1:measurement-end' and e['ph'] == 'I']
    assert len(starts) == len(ends) == 1
    begin, end = starts[0]['ts'], ends[0]['ts']
    compilation = {}
    for name in ['wasm.TopTierCompilation', 'wasm.CompileLazy']:
        selected = [e for e in trace if e['name'] == name and e['ph'] == 'X'
                    and e['ts'] >= begin and e['ts'] + e['dur'] <= end]
        crossing = [e for e in trace if e['name'] == name and e['ph'] == 'X'
                    and e['ts'] < end and e['ts'] + e['dur'] > begin and e not in selected]
        by_thread = collections.defaultdict(list)
        for event in selected:
            by_thread[(event['pid'], event['tid'])].append(event)
        for events in by_thread.values():
            events.sort(key=lambda e: e['ts'])
            assert all(a['ts'] + a['dur'] <= b['ts'] for a, b in zip(events, events[1:])), name
        compilation[name] = dict(count=len(selected),
            elapsed_sum_seconds=sum(e['dur'] for e in selected) / 1e6,
            thread_seconds=sum(e.get('tdur', 0) for e in selected) / 1e6,
            missing_thread_duration=sum('tdur' not in e for e in selected),
            boundary_crossing_count=len(crossing))
    mmu_frames = [f for f in guest['frames'] if 'mmu_base::mmu_base' in f['name']]
    hashes = {f: hashlib.sha256((capture / f).read_bytes()).hexdigest()
              for f in ['trace.json', 'chrome-profile.json', 'guest.cpuprofile', 'report.json']}
    rows.append(dict(name=capture.name, artifacts=str(capture), hashes=hashes,
        wasm_sha256=report['wasm_sha256'], loader_sha256=report['loader_sha256'],
        measurement=report['measurement'], guest_worker=chrome['likely_guest_worker'],
        sampled_us=guest['sampled_us'], generated_self_us=guest['generated_self_us'],
        mmu_callback_sampled_us=sum(f['self_us'] for f in mmu_frames),
        mmu_callback_frames=mmu_frames, top_self_frames=guest['frames'][:12],
        marker_interval_seconds=(end-begin)/1e6, compilation=compilation,
        command=json.loads((root / (capture.name+'-command.json')).read_text())))
(root / 'profile-summary.json').write_text(json.dumps(rows, indent=2)+'\n')
for row in rows:
    print(row['name'], row['mmu_callback_sampled_us'], row['compilation'])
