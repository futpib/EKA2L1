#!/usr/bin/env python3
"""Compare all pixels and guest timing; report the first divergence, never a tolerance pass."""
import argparse
import json
from pathlib import Path
from run_native import frame_records

p = argparse.ArgumentParser()
p.add_argument('reference', type=Path)
p.add_argument('actual', type=Path)
a = p.parse_args()
reference_lines = (a.reference / 'frames.jsonl').read_text().splitlines()
if not reference_lines:
    raise SystemExit('FAIL: empty reference')
reference = frame_records(a.reference, len(reference_lines))
actual = frame_records(a.actual, len(reference_lines))
differences = {key: sum(x[key] != y[key] for x, y in zip(reference, actual))
               for key in reference[0]}
for expected, got in zip(reference, actual):
    if expected != got:
        i = expected['frame']
        print(json.dumps({'match': False, 'frames': len(reference), 'differing_frames_by_field': differences,
                          'first_divergence': i, 'expected': expected, 'actual': got}, indent=2))
        raise SystemExit(1)
print(json.dumps({'match': True, 'frames': len(reference), 'unique_images': len({r['sha256_rgba'] for r in reference}),
                  'last_virtual_us': reference[-1]['virtual_us'], 'instructions': reference[-1]['instructions']}, indent=2))
