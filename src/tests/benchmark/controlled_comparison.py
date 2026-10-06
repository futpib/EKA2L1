#!/usr/bin/env python3
"""Serial frozen-build comparisons with measured frequency and worker affinity.

Use fixed_frequency.py around this runner. The JSON plan records the experiments,
both orders and objective clock-validity thresholds before measurement. A clock
failure is retained and retried, never counted as an optimization loss.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from fixed_frequency import cpu_set


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_clock(report, plan):
    affinity = report['worker_affinity']
    worker = next(row for row in report['thread_deltas']
                  if row['pid'] == affinity['pid'] and row['tid'] == affinity['tid'])
    errors = []
    if worker != max(report['thread_deltas'], key=lambda row: row['runtime_ns']):
        errors.append('Pinned thread was not the dominant measured worker')
    hardware = worker['hardware']
    if not hardware:
        return dict(valid=False, errors=['Missing guest worker counters'])
    if hardware.get('start_ticks') != affinity['start_ticks']:
        errors.append('Hardware worker identity mismatch')
    for event in ('user_cycles', 'user_instructions', 'user_reference_cycles'):
        if not hardware.get(event) or hardware[event]['running_fraction'] != 1:
            errors.append(f'Missing or multiplexed {event}')
    if errors:
        return dict(valid=False, errors=errors)
    if hardware['user_reference_cycles']['raw'] <= 0:
        return dict(valid=False, errors=['Empty reference-cycle measurement'])
    mhz = plan['reference_mhz'] * hardware['user_cycles']['raw'] / hardware['user_reference_cycles']['raw']
    target = plan['frequency_khz'] / 1000
    rules = plan['clock_rules']
    if abs(mhz / target - 1) > rules['mean_relative_tolerance']:
        errors.append('Whole-window frequency outside predeclared limit')
    samples = report['clock_samples']
    intervals = []
    for row in samples:
        if row.get('error'):
            errors.append('Clock sample failed: ' + row['error'])
        if row.get('affinity') != [plan['worker_cpu']]:
            errors.append('Worker affinity changed')
        if row.get('policy') != dict(scaling_governor='performance',
                scaling_min_freq=str(plan['frequency_khz']), scaling_max_freq=str(plan['frequency_khz'])):
            errors.append('Requested CPU policy changed')
        if plan.get('isolated_cpus'):
            groups = row.get('cgroup_cpus', {})
            if not groups:
                errors.append('Missing CPU isolation observation')
            for path, allowed in groups.items():
                if '/ekabench.slice/' not in path and cpu_set(allowed) & set(plan['isolated_cpus']):
                    errors.append('Other cgroup can run on the reserved core')
    for first, last in zip(samples, samples[1:]):
        if first.get('error') or last.get('error'):
            continue
        reference = last['user_reference_cycles'][0] - first['user_reference_cycles'][0]
        if reference > plan['reference_mhz'] * 1e6 * rules['minimum_reference_interval_seconds']:
            value = plan['reference_mhz'] * (last['user_cycles'][0] - first['user_cycles'][0]) / reference
            intervals.append(value)
            if abs(value / target - 1) > rules['interval_relative_tolerance']:
                errors.append('Interval frequency outside predeclared limit')
    if not intervals:
        errors.append('No frequency intervals')
    for name in ('core_throttle_count', 'package_throttle_count'):
        if samples and name in samples[0] and samples[-1].get(name) != samples[0][name]:
            errors.append(f'{name} increased')
    sibling_busy = {}
    if samples and plan.get('isolated_cpus'):
        for cpu in set(plan['isolated_cpus']) - {plan['worker_cpu']}:
            first = samples[0]['cpu_ticks'][f'cpu{cpu}'][:8]
            last = samples[-1]['cpu_ticks'][f'cpu{cpu}'][:8]
            total = sum(last) - sum(first)
            idle = last[3] + last[4] - first[3] - first[4]
            sibling_busy[str(cpu)] = (total - idle) / total if total else 0
            if sibling_busy[str(cpu)] > rules['max_sibling_busy_fraction']:
                errors.append('Reserved sibling CPU was busy')
    return dict(valid=not errors, errors=sorted(set(errors)), mhz=mhz,
                interval_min_mhz=min(intervals, default=None), interval_max_mhz=max(intervals, default=None),
                worker_cpu_seconds=worker['runtime_ns'] / 1e9,
                instructions=hardware['user_instructions']['raw'], cycles=hardware['user_cycles']['raw'],
                worker=affinity['key'], sibling_busy_fraction=sibling_busy)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--only', nargs='+')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    plan = json.loads(args.plan.read_text())
    known = {experiment['name'] for experiment in plan['experiments']}
    if args.only and not set(args.only) <= known:
        parser.error('Unknown experiment in --only')
    if plan.get('isolated_cpus'):
        siblings = cpu_set(Path(f"/sys/devices/system/cpu/cpu{plan['worker_cpu']}/topology/thread_siblings_list").read_text())
        if not siblings <= set(plan['isolated_cpus']) or set(plan['support_cpus']) & set(plan['isolated_cpus']):
            parser.error('Reserve every worker sibling and exclude them from support CPUs')
    args.output.mkdir(exist_ok=True, parents=True)
    evidence = args.output / 'observations.json'
    rows = json.loads(evidence.read_text()) if evidence.exists() else []
    plan_hash = digest(args.plan)
    controller_hash = digest(Path(__file__))
    for experiment in plan['experiments']:
        if args.only and experiment['name'] not in args.only:
            continue
        builds = {key: Path(experiment[key]) for key in ('control', 'candidate')}
        hashes = {key: {file: digest(path / file) for file in ('eka2l1.wasm', 'eka2l1.js')}
                  for key, path in builds.items()}
        for game in experiment['games']:
            journal = None
            work = None
            for panel, order in enumerate(plan['panels']):
                for index, symbol in enumerate(order):
                    variant = 'control' if symbol == 'A' else 'candidate'
                    name = f"{experiment['name']}-{game}-{panel}-{index}-{variant}"
                    old = [row for row in rows if row['name'] == name and row['validity']['valid']]
                    if old:
                        if old[0]['plan_sha256'] != plan_hash:
                            raise RuntimeError('Plan changed during a resumed campaign')
                        journal, work = old[0]['journal_sha256'], old[0]['work']
                        continue
                    attempts = len([row for row in rows if row['name'] == name])
                    for attempt in range(attempts, 3):
                        dest = args.output / (name + f'-attempt{attempt}')
                        env = {k: v for k, v in os.environ.items() if not k.startswith('EKA2L1_')}
                        env.update(experiment['env'])
                        env.update(experiment[variant + '_env'])
                        env.update(GIT_DIR=str(repo / '.git'), GIT_WORK_TREE=str(repo))
                        env['EKA2L1_PROFILE_INPUT'] = str(repo / 'src/tests/benchmark' /
                            ('snakes.input' if game == 'standard' else 'sky-force-combat.input'))
                        assets = Path('/home/claude/.scratch') / ('eka-benchmark/assets' if game == 'standard'
                                                                 else 'eka-sky-performance/assets')
                        if game == 'combat':
                            env.update(EKA2L1_APP_UID='0xa020d913',
                                EKA2L1_ASSET_MANIFEST=str(repo / 'src/tests/benchmark/sky-force-assets.json'))
                        if experiment['window'] == 'warm':
                            start, end = (78000000, 96000000) if game == 'standard' else (42000000, 60000000)
                        else:
                            start, end = (21000000, 25000000) if game == 'standard' else (42000000, 48000000)
                        if game in experiment.get('windows', {}):
                            start, end = experiment['windows'][game]
                        command = ['python3', str(repo / 'src/tests/benchmark/scheduler_probe.py'),
                            str(assets), str(builds[variant]), str(dest), '--hardware-counters',
                            '--start-us', str(start), '--end-us', str(end), '--capture-mode', str(experiment.get('capture_mode', 1)),
                            '--worker-cpu', str(plan['worker_cpu']), '--support-cpus',
                            ','.join(map(str, plan['support_cpus'])), '--reference-mhz', str(plan['reference_mhz']),
                            '--harness', experiment['harness']]
                        metadata = dict(args=command, env={k: v for k, v in env.items()
                            if k.startswith('EKA2L1_') or k in ('GIT_DIR', 'GIT_WORK_TREE')},
                            hashes=hashes, plan_sha256=plan_hash, controller_sha256=controller_hash,
                            harness_hashes={str(p): digest(p) for p in [
                                Path(__file__), repo / 'src/tests/benchmark/scheduler_probe.py',
                                *sorted(Path(experiment['harness']).glob('*.ts'))]})
                        dest.with_suffix('.command.json').write_text(json.dumps(metadata, indent=2) + '\n')
                        print('START', dest.name, flush=True)
                        with dest.with_suffix('.log').open('w') as log:
                            subprocess.run(command, cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT,
                                           timeout=900, check=True)
                        scheduler = json.loads((dest / 'scheduler.json').read_text())
                        report = json.loads((dest / 'profile/report.json').read_text())
                        if (report['wasm_sha256'] != hashes[variant]['eka2l1.wasm'] or
                                report['loader_sha256'] != hashes[variant]['eka2l1.js']):
                            raise RuntimeError('Browser build hash mismatch')
                        for key, value in experiment.get('expected_' + variant, {}).items():
                            if report.get(key) != value:
                                raise RuntimeError(f'Experiment selection mismatch: {key} expected {value}, got {report.get(key)}')
                        measurement = report['measurement']
                        current_work = {k: measurement[k] for k in ('first_virtual_us', 'last_virtual_us',
                                        'first_instructions', 'last_instructions', 'presentations')}
                        journal_path = dest / 'profile/frames.jsonl'
                        if not journal_path.exists() and experiment.get('capture_mode', 1) != 2:
                            raise RuntimeError('Missing presentation journal')
                        current_journal = digest(journal_path) if journal_path.exists() else None
                        if work is not None and (current_work != work or current_journal != journal):
                            raise RuntimeError('Guest work or presentation journal mismatch')
                        work, journal = current_work, current_journal
                        validity = validate_clock(scheduler, plan)
                        browser_worker = report.get('cpu_time', {}).get('busiest_renderer_thread')
                        if browser_worker and f"{browser_worker['pid']}:{browser_worker['tid']}" != validity.get('worker'):
                            validity['valid'] = False
                            validity['errors'].append('Browser worker identity mismatch')
                        if (report['sampling'] or report['verify_aot'] or report['guest_profile_stride'] or
                                report.get('chrome_trace', {}).get('scope', 'off') != 'off' or measurement['detailed']):
                            raise RuntimeError('Instrumented game timing')
                        if 'NVIDIA' not in report['renderer']:
                            raise RuntimeError('Expected the hardware NVIDIA renderer')
                        rows.append(dict(name=name, experiment=experiment['name'], game=game, panel=panel,
                            index=index, variant=variant, attempt=attempt, directory=str(dest),
                            validity=validity, work=work, journal_sha256=journal, report=report,
                            plan_sha256=plan_hash, command=metadata, scheduler_sha256=digest(dest / 'scheduler.json')))
                        evidence.write_text(json.dumps(rows, indent=2) + '\n')
                        print('PASS' if validity['valid'] else 'INVALID', dest.name,
                              'CPU', validity.get('worker_cpu_seconds'), 'MHz', validity.get('mhz'),
                              'errors', validity['errors'], flush=True)
                        if validity['valid']:
                            break
                        time.sleep(2)
                    else:
                        raise RuntimeError(f'Three invalid attempts: {name}')


if __name__ == '__main__':
    main()
