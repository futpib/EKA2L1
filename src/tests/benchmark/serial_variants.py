#!/usr/bin/env python3
"""Measure archived builds serially in forward/reverse order, including warmup."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('assets', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('builds', nargs='+', help='NAME=ARCHIVED_BUILD')
args = parser.parse_args()
variants = []
for item in args.builds:
    name, path = item.split('=', 1)
    if not name.replace('-', '').isalnum() or any(name == n for n, _ in variants):
        parser.error('Unique alphanumeric/hyphen names are required')
    variants.append((name, Path(path).resolve()))
args.output.mkdir()
root = Path(__file__).resolve().parents[3]
rows = []
for repetition, order in ((1, variants), (2, list(reversed(variants)))):
    for name, build in order:
        label = f'{name}-{repetition}'
        env = dict(os.environ, EKA2L1_WASM_BUILD_DIR=str(build),
                   EKA2L1_BENCHMARK_AOT='5', EKA2L1_GPU='hardware',
                   EKA2L1_PROFILE_DETAIL='0', EKA2L1_PROFILE_START_US='78000000')
        for key in ('PROFILE_GATE', 'EKA2L1_AOT_VERIFY', 'EKA2L1_GUEST_PROFILE',
                    'EKA2L1_AOT_DIAGNOSTICS', 'EKA2L1_V8_FLAGS', 'EKA2L1_V8_DUMP'):
            env.pop(key, None)
        output = args.output / label
        with (args.output / (label + '.log')).open('w') as log:
            subprocess.run(['node', 'profile.ts', str(args.assets.resolve()),
                            str(output.resolve()), '2', '0', '96000000'],
                           cwd=root / 'src/tests/wasm', env=env, stdout=log,
                           stderr=subprocess.STDOUT, timeout=1800, check=True)
        report = json.loads((output / 'report.json').read_text())
        row = dict(name=label, build=str(build), measurement=report['measurement'])
        rows.append(row)
        (args.output / 'measurements.json').write_text(json.dumps(rows, indent=2) + '\n')
        print(json.dumps(row), flush=True)
