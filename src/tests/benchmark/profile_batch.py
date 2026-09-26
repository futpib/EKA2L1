#!/usr/bin/env python3
"""Warm browser fixtures together, then measure one paused fixture at a time."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser()
p.add_argument('--assets', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
plan = [('full-1', 0), ('no-png', 1), ('no-readback', 2), ('full-2', 0)]
processes = []
logs = []
try:
    for name, mode in plan:
        gate = a.output / f'{name}.release'
        log = (a.output / f'{name}.log').open('w')
        logs.append(log)
        process = subprocess.Popen(['node', 'profile.ts', str(a.assets.resolve()), str(a.output/name), str(mode), '0'],
            cwd=ROOT/'src/tests/wasm', env={**os.environ, 'PROFILE_GATE':str(gate)}, stdout=log, stderr=subprocess.STDOUT)
        processes.append((name, process, gate))
    deadline = time.monotonic() + 1800
    while not all(Path(str(gate)+'.ready').exists() for _, _, gate in processes):
        for name, process, _ in processes:
            if process.poll() is not None:
                raise RuntimeError(f'{name} exited during warmup: {process.returncode}')
        if time.monotonic() > deadline:
            raise RuntimeError('Parallel warmup timed out')
        time.sleep(1)
    print('All guests paused at 21 seconds. Starting serial measurements.', flush=True)
    for name, process, gate in processes:
        gate.touch()
        if process.wait(timeout=1800):
            raise RuntimeError(f'{name} failed; see its log')
        result = json.loads((a.output/name/'report.json').read_text())
        print(name, json.dumps(result['measurement']), flush=True)
finally:
    for _, process, gate in processes:
        if process.poll() is None:
            # Release the JS runner first so it can observe its own timeout/errors.
            gate.touch(exist_ok=True)
            process.terminate()
    for log in logs:
        log.close()
