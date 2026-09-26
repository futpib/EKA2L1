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
modes = p.add_mutually_exclusive_group()
modes.add_argument('--compare-aot', action='store_true', help='Compare interpreter, exports and hot-ROM compilation with timing repeats')
modes.add_argument('--compare-diagnostics', action='store_true', help='Measure optional AOT bookkeeping with paired controls')
modes.add_argument('--compare-build', type=Path, help='Compare an archived frontend build with the current build in AOT mode 4 (old/new/new/old)')
modes.add_argument('--compare-steps', type=Path, nargs=2, metavar=('BASELINE', 'STEP1'), help='Compare baseline, step 1 and current build serially in forward/reverse order')
modes.add_argument('--compare-stages', action='store_true', help='Compare interpreter, hot ROM, RAM and chained/register-cached execution')
p.add_argument('--measure-gate', type=Path, help='Wait for this new gate file after all fixtures are paused')
a = p.parse_args()
if a.measure_gate:
    a.measure_gate = a.measure_gate.resolve()
    if a.measure_gate.exists() or Path(str(a.measure_gate) + '.ready').exists():
        p.error('Measurement gate and ready marker must be new paths')
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
plan = [('full-1', 0, None), ('no-png', 1, None), ('no-readback', 2, None), ('full-2', 0, None)]
if a.compare_aot:
    plan = [('interpreter-1', 0, 0), ('exports', 0, 1), ('hot-rom-1', 0, 2), ('hot-rom-2', 0, 2), ('interpreter-2', 0, 0)]
if a.compare_diagnostics:
    plan = [('diagnostics-off-1', 0, 2), ('diagnostics-on', 0, 2), ('diagnostics-off-2', 0, 2)]
if a.compare_stages:
    plan = [('interpreter-1', 0, 0), ('hot-rom', 0, 2), ('hot-ram', 0, 3),
            ('chained-1', 0, 4), ('chained-2', 0, 4), ('interpreter-2', 0, 0)]
if a.compare_build:
    a.compare_build = a.compare_build.resolve()
    if not (a.compare_build / 'eka2l1.wasm').is_file():
        p.error('Archived build must contain eka2l1.wasm')
    plan = [('before-1', 0, 4), ('after-1', 0, 4), ('after-2', 0, 4), ('before-2', 0, 4)]
if a.compare_steps:
    a.compare_steps = [path.resolve() for path in a.compare_steps]
    if not all((path / 'eka2l1.wasm').is_file() for path in a.compare_steps):
        p.error('Each archived step must contain eka2l1.wasm')
    plan = [('baseline-1', 0, 4), ('step1-1', 0, 4), ('combined-1', 0, 4),
            ('combined-2', 0, 4), ('step1-2', 0, 4), ('baseline-2', 0, 4)]
processes = []
logs = []
try:
    for name, mode, aot_mode in plan:
        gate = a.output / f'{name}.release'
        log = (a.output / f'{name}.log').open('w')
        logs.append(log)
        environment = {**os.environ, 'PROFILE_GATE': str(gate)}
        if a.compare_build:
            environment.pop('EKA2L1_GUEST_PROFILE', None)
            environment.pop('EKA2L1_WASM_BUILD_DIR', None)
            if name.startswith('before-'):
                environment['EKA2L1_WASM_BUILD_DIR'] = str(a.compare_build)
        if a.compare_steps:
            environment.pop('EKA2L1_GUEST_PROFILE', None)
            environment.pop('EKA2L1_WASM_BUILD_DIR', None)
            if name.startswith('baseline-'):
                environment['EKA2L1_WASM_BUILD_DIR'] = str(a.compare_steps[0])
            elif name.startswith('step1-'):
                environment['EKA2L1_WASM_BUILD_DIR'] = str(a.compare_steps[1])
        if a.compare_diagnostics:
            environment['EKA2L1_AOT_DIAGNOSTICS'] = '1' if name == 'diagnostics-on' else '0'
        if aot_mode is not None:
            environment['EKA2L1_BENCHMARK_AOT'] = str(aot_mode)
            environment.pop('EKA2L1_AOT_VERIFY', None)
            if not a.compare_diagnostics:
                environment['EKA2L1_AOT_DIAGNOSTICS'] = '0'
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
    if a.measure_gate:
        Path(str(a.measure_gate) + '.ready').touch()
        print('All guests paused; waiting for external measurement gate.', flush=True)
        deadline = time.monotonic() + 3600
        while not a.measure_gate.exists():
            if time.monotonic() > deadline:
                raise RuntimeError('External measurement gate timed out')
            if any(process.poll() is not None for _, process, _ in processes):
                raise RuntimeError('A fixture exited while waiting for external gate')
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
