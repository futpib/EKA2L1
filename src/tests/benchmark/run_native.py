#!/usr/bin/env python3
"""Run Snakes from a fresh device snapshot and compare every captured RGBA frame."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
from PIL import Image
from validate_audio import audio_record

ROOT = Path(__file__).resolve().parents[3]
ASSETS = {
    'SYM.ROM': '89c2d9fbbdaa94fca5d8bf49eb512cc82abdc17c97372bca77d700f02bb0d490',
    'SYM.RPKG': '58964f3d08a542f01118a7dfb78a34d2e029962b8edb9988381a37994c1c1531',
    'Snakes.sis': '14d9a40768ae2231ad1905e96bcedefe7ce97b7fc4e610bb924ff8037a6cf82a',
}

def run(args, env, log, timeout):
    with log.open('w') as out:
        start = time.monotonic()
        result = subprocess.Popen(['xvfb-run', '-a', '-s', '-screen 0 800x600x24', *map(str, args)],
                                  env=env, stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            result.wait(timeout=timeout)
        except BaseException:
            try:
                os.killpg(result.pid, signal.SIGTERM)
                result.wait(timeout=5)
            except (subprocess.TimeoutExpired, ProcessLookupError):
                pass
            # xvfb-run can exit before a child that ignores SIGTERM.
            try:
                os.killpg(result.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            result.wait()
            raise
    return result.returncode, time.monotonic() - start


def frame_records(directory, expected):
    records = [json.loads(line) for line in (directory / 'frames.jsonl').read_text().splitlines()]
    if len(records) != expected:
        raise RuntimeError(f'Expected {expected} frames, got {len(records)}')
    for i, record in enumerate(records):
        if record['frame'] != i:
            raise RuntimeError('Non-consecutive capture')
        with Image.open(directory / f'frame-{i:04d}.png') as image:
            if image.size != (record['width'], record['height']):
                raise RuntimeError('Wrong frame dimensions')
            record['sha256_rgba'] = hashlib.sha256(image.convert('RGBA').tobytes()).hexdigest()
    return records


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--assets', type=Path, required=True)
    p.add_argument('--asset-manifest', type=Path,
                   help='JSON with assets keyed by filename, each containing sha256; defaults to Nokia 5320')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--binary', type=Path, default=ROOT / 'build/bin/eka2l1_qt')
    p.add_argument('--input', type=Path, default=Path(__file__).with_name('snakes.input'))
    p.add_argument('--frames', type=int, default=1000)
    p.add_argument('--repeat', type=int, default=2)
    p.add_argument('--timeout', type=int, default=1800)
    p.add_argument('--start-us', type=int, default=21000000)
    p.add_argument('--all-presentations', action='store_true')
    a = p.parse_args()
    if not 1 <= a.frames <= 100000 or a.repeat < 1 or a.timeout < 1 or not 0 <= a.start_us <= 120000000:
        p.error("frames must be 1..100000; repeat and timeout must be positive")
    assets = ASSETS
    if a.asset_manifest:
        manifest = json.loads(a.asset_manifest.read_text())
        assets = {name: manifest['assets'][name]['sha256'] for name in ASSETS}
    for name, expected in assets.items():
        if hashlib.sha256((a.assets / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Asset hash mismatch: {name}')
    a.output = a.output.resolve()
    a.output.mkdir(parents=True, exist_ok=False)
    a.assets = a.assets.resolve()
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM='xcb', LIBGL_ALWAYS_SOFTWARE='1', TZ='UTC',
               XDG_DATA_HOME=str(a.output / 'template/data'), XDG_CONFIG_HOME=str(a.output / 'template/config'),
               EKA2L1_BENCHMARK='1', EKA2L1_BENCHMARK_FRAMES=str(a.frames),
               EKA2L1_BENCHMARK_INPUT=str(a.input.resolve()),
               EKA2L1_BENCHMARK_START_US=str(a.start_us),
               EKA2L1_BENCHMARK_UNIQUE='0' if a.all_presentations else '1')
    code, _ = run([a.binary, '--installdevice', a.assets / 'SYM.ROM', a.assets / 'SYM.RPKG'], env,
                  a.output / 'install.log', a.timeout)
    # The existing CLI returns 255 even for successful install-only commands.
    if code not in (0, 255) or 'Device installed:' not in (a.output / 'install.log').read_text():
        raise RuntimeError('Device install failed; see install.log')
    report = {'shared_audio': env.get('EKA2L1_SHARED_AUDIO') == '1',
              'snakes_n80_native_resolution': env.get('EKA2L1_SNAKES_N80_NATIVE_RESOLUTION') == '1',
              'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'dirty_worktree': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
              'binary_sha256': hashlib.sha256(a.binary.read_bytes()).hexdigest(),
              'assets': assets, 'input_sha256': hashlib.sha256(a.input.read_bytes()).hexdigest(),
              'frames': a.frames, 'start_us': a.start_us, 'unique': not a.all_presentations, 'runs': []}
    baseline = None
    baseline_audio = None
    for i in range(a.repeat):
        directory = a.output / f'run-{i}'
        shutil.copytree(a.output / 'template', directory / 'state')
        frames = directory / 'frames'
        frames.mkdir()
        env.update(XDG_DATA_HOME=str(directory / 'state/data'), XDG_CONFIG_HOME=str(directory / 'state/config'))
        # N80 firmware also registers a different built-in game named Snakes.
        code, elapsed = run([a.binary, '--install', a.assets / 'Snakes.sis', '--run', '0x2000730F',
                             '--dump-frames', frames], env, directory / 'run.log', a.timeout)
        if code:
            raise RuntimeError(f'Run {i} exited {code}; see {directory}/run.log')
        records = frame_records(frames, a.frames)
        (directory / 'frames.json').write_text(json.dumps(records, indent=2) + '\n')
        if baseline is None:
            baseline = records
        elif baseline != records:
            first = next(j for j, (x, y) in enumerate(zip(baseline, records)) if x != y)
            raise RuntimeError(f'Run {i} differs at frame {first}: {baseline[first]} != {records[first]}')
        audio = audio_record(frames, records[0]['virtual_us'], records[-1]['virtual_us'])
        if baseline_audio is not None and audio != baseline_audio:
            raise RuntimeError(f'Run {i} audio differs: {audio} != {baseline_audio}')
        baseline_audio = audio
        (directory / 'audio.json').write_text(json.dumps(audio, indent=2) + '\n')
        report['runs'].append({'wall_seconds': elapsed, 'last_virtual_us': records[-1]['virtual_us']})
        print(f'Run {i}: {len(records)} frames in {elapsed:.3f}s', flush=True)
        (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: all frame pixels, guest timestamps, PCM and audio events match' if a.repeat > 1
          else 'PASS: captured frames and validated guest-clock audio', flush=True)

if __name__ == '__main__':
    main()
