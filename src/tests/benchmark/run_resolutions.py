#!/usr/bin/env python3
"""Probe Snakes with different guest display sizes in fresh device copies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from run_native import ASSETS, run, frame_records


def configure_screen(path, width, height):
    text = path.read_text(encoding='utf-16')
    for mode, (w, h) in enumerate(((width, height), (height, width), (height, width)), 1):
        for key, value in ((f'SCR_WIDTH{mode}', w), (f'SCR_HEIGHT{mode}', h)):
            text, count = re.subn(rf'(?m)^{key}\s+\d+', f'{key} {value}', text)
            if count != 1:
                raise RuntimeError(f'Expected exactly one {key} in {path}')
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-16'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--template', type=Path, required=True,
                   help='Fresh install template/data and template/config from run_native.py')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--sizes', nargs='+', default=['240x320', '320x240', '640x480', '800x352', '1280x720'])
    p.add_argument('--frames', type=int, default=60)
    p.add_argument('--timeout', type=int, default=240)
    p.add_argument('--start-us', type=int, default=21000000)
    a = p.parse_args()
    sizes = []
    for size in a.sizes:
        if not re.fullmatch(r'[1-9][0-9]*x[1-9][0-9]*', size):
            p.error(f'Invalid size: {size}')
        sizes.append(tuple(map(int, size.split('x'))))
    if (len(set(sizes)) != len(sizes) or not 2 <= a.frames <= 100000
            or a.timeout < 1 or not 0 <= a.start_us <= 120000000):
        p.error('Sizes must be unique; frames 2..100000, timeout > 0, start-us 0..120000000')
    for name, expected in ASSETS.items():
        if hashlib.sha256((a.assets / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Asset hash mismatch: {name}')
    for attr in ('assets', 'binary', 'template', 'output'):
        setattr(a, attr, getattr(a, attr).resolve())
    a.output.mkdir(parents=True, exist_ok=False)
    replay = Path(__file__).with_name('snakes.input').resolve()
    report = {'binary_sha256': hashlib.sha256(a.binary.read_bytes()).hexdigest(),
              'assets': ASSETS, 'input_sha256': hashlib.sha256(replay.read_bytes()).hexdigest(),
              'frames_requested': a.frames, 'start_us': a.start_us,
              'method': 'wsini pixel sizes only; original rotation, twips and S60 layout names retained',
              'runs': []}
    for width, height in sizes:
        directory = a.output / f'{width}x{height}'
        state = directory / 'state'
        shutil.copytree(a.template, state)
        root = state / 'data/EKA2L1'
        wsini = root / 'data/drives/z/rm-409/system/data/wsini.ini'
        configure_screen(wsini, width, height)
        shutil.copy2(wsini, directory / 'wsini.ini')
        (root / 'config.yml').write_text('log-filter: "*:info Kernel:trace Emulated.Stdout:trace"\n')
        frames = directory / 'frames'
        frames.mkdir()
        env = {key: value for key, value in os.environ.items() if not key.startswith('EKA2L1_')}
        env.update(QT_QPA_PLATFORM='xcb', LIBGL_ALWAYS_SOFTWARE='1', TZ='UTC',
                   XDG_DATA_HOME=str(state / 'data'), XDG_CONFIG_HOME=str(state / 'config'),
                   EKA2L1_BENCHMARK='1', EKA2L1_BENCHMARK_FRAMES=str(a.frames),
                   EKA2L1_BENCHMARK_INPUT=str(replay), EKA2L1_BENCHMARK_START_US=str(a.start_us),
                   EKA2L1_BENCHMARK_UNIQUE='1')
        entry = {'configured_mode1': [width, height]}
        started = time.monotonic()
        print(f'Running {width}x{height}', flush=True)
        try:
            code, elapsed = run([a.binary, '--install', a.assets / 'Snakes.sis', '--run', 'Snakes',
                                 '--dump-frames', frames], env, directory / 'run.log', a.timeout)
            entry.update(exit_code=code, wall_seconds=elapsed)
        except subprocess.TimeoutExpired:
            entry.update(timed_out=True, wall_seconds=time.monotonic() - started)
        log = (directory / 'run.log').read_text(errors='replace')
        entry['game_screen_diagnostics'] = [line for line in log.splitlines()
                                            if 'CGameHarness' in line or 'back buffer size' in line]
        entry['errors'] = [line for line in log.splitlines()
                           if line.startswith('E ') or 'panic' in line.lower()]
        entry['guest_panics'] = [line for line in log.splitlines() if 'panicked with category:' in line]
        manifest = frames / 'frames.jsonl'
        count = len(manifest.read_text().splitlines()) if manifest.exists() else 0
        entry['captured_frames'] = count
        entry['capture_complete'] = (entry.get('exit_code') == 0
                                     and count == a.frames and not entry['guest_panics'])
        if count:
            records = frame_records(frames, count)
            entry['captured_sizes'] = sorted({(r['width'], r['height']) for r in records})
            entry['first_virtual_us'] = records[0]['virtual_us']
            entry['last_virtual_us'] = records[-1]['virtual_us']
            (directory / 'frames.json').write_text(json.dumps(records, indent=2) + '\n')
        report['runs'].append(entry)
        (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({key: value for key, value in entry.items()
                          if key not in ('errors', 'game_screen_diagnostics')}), flush=True)


if __name__ == '__main__':
    main()
