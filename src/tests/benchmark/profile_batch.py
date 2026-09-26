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
p.add_argument('--compare-aot', action='store_true', help='Compare interpreter, exports and hot-ROM compilation with timing repeats')
p.add_argument('--compare-diagnostics', action='store_true', help='Measure optional AOT bookkeeping with paired controls')
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
plan = [('full-1', 0, None), ('no-png', 1, None), ('no-readback', 2, None), ('full-2', 0, None)]
if a.compare_aot:
    plan = [('interpreter-1', 0, 0), ('exports', 0, 1), ('hot-rom-1', 0, 2), ('hot-rom-2', 0, 2), ('interpreter-2', 0, 0)]
if a.compare_diagnostics:
    plan = [('diagnostics-off-1', 0, 2), ('diagnostics-on', 0, 2), ('diagnostics-off-2', 0, 2)]
processes = []
logs = []
try:
    for name, mode, aot_mode in plan:
        gate = a.output / f'{name}.release'
        log = (a.output / f'{name}.log').open('w')
        logs.append(log)
        environment = {**os.environ, 'PROFILE_GATE': str(gate)}
        if a.compare_diagnostics:
            environment['EKA2L1_AOT_DIAGNOSTICS'] = '1' if name == 'diagnostics-on' else '0'
        if aot_mode is not None:
            environment['EKA2L1_BENCHMARK_AOT'] = str(aot_mode)
            environment.pop('EKA2L1_AOT_VERIFY', None)
        process = subprocess.Popen(['node', 'profile.ts', str(a.assets.resolve()), str(a.output/name), str(mode), '0'],
            cwd=ROOT/'src/tests/wasm', env=environment, stdout=log, stderr=subprocess.STDOUT)
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
