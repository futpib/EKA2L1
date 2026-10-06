#!/usr/bin/env python3
"""Compare two frozen browser builds serially on identical guest memory workloads."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline', type=Path)
p.add_argument('candidate', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--snakes-assets', type=Path, required=True)
p.add_argument('--sky-assets', type=Path, required=True)
p.add_argument('--reference-root', type=Path, required=True)
p.add_argument('--games', nargs='+', choices=['standard', 'combat'], default=['standard', 'combat'])
p.add_argument('--modes', nargs='+', type=int, choices=[0, 2], default=[0, 2])
p.add_argument('--rounds', type=int, default=2)
p.add_argument('--window-us', type=int, help='Override the fixed guest-time measurement window')
a = p.parse_args()
if a.rounds < 1:
    p.error('Rounds must be positive')
a.output.mkdir(parents=True, exist_ok=False)
repo = Path(__file__).resolve().parents[3]
builds = {'baseline': a.baseline.resolve(), 'candidate': a.candidate.resolve()}
hashes = {name: {file: hashlib.sha256((build / file).read_bytes()).hexdigest()
                for file in ['eka2l1.wasm', 'eka2l1.js']} for name, build in builds.items()}
(a.output / 'build-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
rows = []
for game in a.games:
    journal = None
    for mode in a.modes:
        for repetition in range(a.rounds):
            order = ['baseline', 'candidate'] if repetition % 2 == 0 else ['candidate', 'baseline']
            for variant in order:
                name = f'{game}-{mode}-{repetition}-{variant}'
                output = a.output / name
                args = ['python3', str(repo / 'src/tests/benchmark/memory_implementations.py'),
                        str(output.resolve()), 'timings', '--build', str(builds[variant]),
                        '--snakes-assets', str(a.snakes_assets.resolve()),
                        '--sky-assets', str(a.sky_assets.resolve()),
                        '--reference-root', str(a.reference_root.resolve()),
                        '--games', game, '--modes', str(mode), '--rounds', '1']
                if a.window_us is not None:
                    args += ['--window-us', str(a.window_us)]
                print('START', name, flush=True)
                with (a.output / (name + '.log')).open('w') as log:
                    subprocess.run(args, cwd=repo, stdout=log, stderr=subprocess.STDOUT,
                                   timeout=1800, check=True)
                result = json.loads((output / 'results.json').read_text())
                assert len(result) == 1
                row = result[0]
                report = row['report']
                assert report['wasm_sha256'] == hashes[variant]['eka2l1.wasm']
                assert report['loader_sha256'] == hashes[variant]['eka2l1.js']
                current = (output / row['name'] / 'frames.jsonl').read_bytes()
                if journal is None:
                    journal = current
                assert current == journal, 'Presentation journal differs between builds or backends'
                row.update(variant=variant, repetition=repetition, run=name,
                           presentation_journal_sha256=hashlib.sha256(current).hexdigest())
                rows.append(row)
                (a.output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
                cpu = report['cpu_time']
                print('PASS', name, 'worker_cpu_s', cpu['busiest_renderer_thread']['cpu_seconds'],
                      'renderer_cpu_s', cpu['renderer_cpu_seconds'], flush=True)
