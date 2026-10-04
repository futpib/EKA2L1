#!/usr/bin/env python3
"""Run the isolated page-cache experiment; requires the patched frozen build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess

VARIANTS = [('none', 0, 1), ('page_only', 2, 1), ('original_tlb', 1, 0),
            ('folded_tlb', 1, 1), ('original_both', 3, 0), ('current', 3, 1),
            ('old_guards', 7, 1), ('span_reuse', 11, 1)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path, help='Contains source/ and build/')
    p.add_argument('phase', choices=['replays', 'timings', 'confirm'])
    p.add_argument('--snakes-assets', type=Path, required=True)
    p.add_argument('--sky-assets', type=Path, required=True)
    p.add_argument('--reference-root', type=Path,
                   help='Contains replay-standard/ and replay-combat/; required for replays')
    a = p.parse_args()
    if a.phase == 'replays' and not a.reference_root:
        p.error('Replays require --reference-root')
    root = a.root.resolve()
    source = root / 'source'
    bench = source / 'src/tests/benchmark'
    build = root / 'build/src/emu/wasm'
    wasm_hash = hashlib.sha256((build / 'eka2l1.wasm').read_bytes()).hexdigest()
    common = dict(EKA2L1_BENCHMARK_AOT='5', EKA2L1_CODE_COMPARE='2',
                  EKA2L1_PREDICATED_LEAVES='1', EKA2L1_LEAF_FEATURES='128',
                  EKA2L1_UNSAFE_CODE='3', EKA2L1_SHARED_AUDIO='1',
                  EKA2L1_GPU='hardware', EKA2L1_ARM_MEMORY='0',
                  EKA2L1_ARM_EXCLUSIVE='0', EKA2L1_AOT_IR_MODE='17',
                  EKA2L1_HOTPATH='2', EKA2L1_THUMB_MEMORY='1',
                  EKA2L1_WASM_BUILD_DIR=str(build))
    repeats = [0] if a.phase == 'replays' else range(2) if a.phase == 'timings' else range(2, 4)
    jobs = [(game, variant, repeat) for repeat in repeats
            for game in ['standard', 'combat']
            for variant in (VARIANTS if repeat % 2 == 0 else VARIANTS[::-1])]
    rows = []
    for index, (game, (name, mode, tlb), repeat) in enumerate(jobs):
        label = f'{a.phase}-{index:02d}-{game}-{name}'
        dest = root / label
        assets = (a.snakes_assets if game == 'standard' else a.sky_assets).resolve()
        start, end = ('21000000', '25000000') if game == 'standard' else ('42000000', '48000000')
        inp = bench / ('snakes.input' if game == 'standard' else 'sky-force-combat.input')
        config = dict(common, EKA2L1_MEMORY_CACHE=str(mode), EKA2L1_TLB_HASH=str(tlb))
        if game == 'combat':
            config.update(EKA2L1_APP_UID='0xa020d913',
                          EKA2L1_ASSET_MANIFEST=str(bench / 'sky-force-assets.json'))
        if a.phase == 'replays':
            args = ['node', 'benchmark.ts', str(assets), str(dest), '60', str(inp), start]
        else:
            args = ['node', 'profile.ts', str(assets), str(dest), '1', '0', end]
            config.update(EKA2L1_PROFILE_START_US=start, EKA2L1_PROFILE_INPUT=str(inp),
                          EKA2L1_CHROME_TRACE='off')
        command = {'args': args, 'env': config, 'cwd': str(source / 'src/tests/wasm')}
        command_file = root / f'{label}-command.json'
        if dest.exists():
            raise RuntimeError(f'Refusing to reuse an existing run: {dest}')
        command_file.write_text(json.dumps(command, indent=2) + '\n')
        env = {k: v for k, v in os.environ.items() if not k.startswith('EKA2L1_')}
        env.update(config)
        print(label + ' START', flush=True)
        with (root / f'{label}.log').open('w') as out:
            child = subprocess.Popen(args, cwd=command['cwd'], env=env, stdout=out,
                                     stderr=subprocess.STDOUT, start_new_session=True)
            try:
                child.wait(timeout=600)
                if child.returncode:
                    raise RuntimeError(f'{label} exited with {child.returncode}')
            except BaseException:
                try:
                    os.killpg(child.pid, signal.SIGTERM)
                    child.wait(timeout=10)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    pass
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                raise
        report = json.loads((dest / 'report.json').read_text())
        assert report['memory_cache'] == mode and report['tlb_hash'] == tlb
        assert report['wasm_sha256'] == wasm_hash
        row = dict(game=game, variant=name, order=index, repeat=repeat,
                   directory=str(dest), command=command, report=report)
        if a.phase == 'replays':
            compared = subprocess.run(['python3', str(bench / 'compare.py'),
                                       str(a.reference_root / f'replay-{game}'), str(dest)],
                                      capture_output=True, text=True)
            (root / f'{label}-compare.json').write_text(compared.stdout + compared.stderr)
            compared.check_returncode()
            row['comparison'] = json.loads(compared.stdout)
            assert row['comparison']['match']
            print(label + ' MATCH', flush=True)
        else:
            assert report['purpose'] == 'throughput' and not report['sampling']
            assert report['chrome_trace']['scope'] == 'off' and not report['diagnostics_available']
            cpu = report['cpu_time']
            assert cpu['renderer_complete'] and not cpu['thread_errors']
            print(f'{label}: wall={report["measurement"]["wall_seconds"]:.6f}s '
                  f'CPU={cpu["busiest_renderer_thread"]["cpu_seconds"]:.6f}s', flush=True)
        rows.append(row)
        (root / f'{a.phase}.json').write_text(json.dumps(rows, indent=2) + '\n')
    if a.phase != 'replays':
        keys = ['first_virtual_us', 'last_virtual_us', 'first_instructions',
                'last_instructions', 'presentations']
        for game in ['standard', 'combat']:
            assert len({tuple(row['report']['measurement'][k] for k in keys)
                        for row in rows if row['game'] == game}) == 1


if __name__ == '__main__':
    main()
