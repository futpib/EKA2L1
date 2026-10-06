#!/usr/bin/env python3
"""Summarize controlled campaigns without hiding incomplete or invalid trials."""
import argparse
import hashlib
import json
from pathlib import Path
from statistics import mean


def change(control, candidate):
    return 100 * (control / candidate - 1)


def summarize(rows, expected):
    valid = [row for row in rows if row['validity']['valid']]
    result = dict(valid=len(valid), invalid=len(rows) - len(valid), expected=expected,
                  complete=len(valid) == expected)
    if not result['complete']:
        return result
    positions = {(row['panel'], row['index']): row for row in valid}
    if len(positions) != len(valid):
        raise ValueError('Duplicate valid observations')
    variants = {v: [row for row in valid if row['variant'] == v] for v in ('control', 'candidate')}
    if len(variants['control']) != len(variants['candidate']):
        raise ValueError('Unbalanced comparison')
    for variant, observations in variants.items():
        result[variant] = {key: mean(row['validity'][key] for row in observations)
                           for key in ('worker_cpu_seconds', 'cycles', 'instructions')}
        result[variant]['wall_seconds'] = mean(row['report']['measurement']['wall_seconds']
                                                for row in observations)
        result[variant]['cpu_seconds_range'] = [min(row['validity']['worker_cpu_seconds'] for row in observations),
                                               max(row['validity']['worker_cpu_seconds'] for row in observations)]
    result['cpu_throughput_change_percent'] = change(result['control']['worker_cpu_seconds'],
                                                     result['candidate']['worker_cpu_seconds'])
    result['wall_throughput_change_percent'] = change(result['control']['wall_seconds'],
                                                      result['candidate']['wall_seconds'])
    result['native_instruction_change_percent'] = 100 * (result['candidate']['instructions'] /
                                                         result['control']['instructions'] - 1)
    pairs = []
    for panel, index in sorted(positions):
        if index % 2:
            continue
        a, b = positions[panel, index], positions[panel, index + 1]
        if a['variant'] == b['variant']:
            raise ValueError('Adjacent observations are not a control/candidate pair')
        control, candidate = (a, b) if a['variant'] == 'control' else (b, a)
        pairs.append(change(control['validity']['worker_cpu_seconds'], candidate['validity']['worker_cpu_seconds']))
    result['paired_cpu_throughput_changes_percent'] = pairs
    result['favorable_pairs'] = sum(value > 0 for value in pairs)
    result['frequency_mhz_range'] = [min(row['validity']['mhz'] for row in valid),
                                     max(row['validity']['mhz'] for row in valid)]
    # This is an observation count, not a significance or generalization claim.
    result['direction'] = ('all pairs faster' if all(value > 0 for value in pairs) else
                           'all pairs slower' if all(value < 0 for value in pairs) else 'mixed pairs')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign_root', type=Path)
    parser.add_argument('output_stem', type=Path)
    parser.add_argument('--phases', nargs='+', default=['isolated', 'architecture', 'extended', 'inlining'])
    args = parser.parse_args()
    data = dict(phases={}, comparisons=[], observations=[])
    for phase in args.phases:
        plan_path = args.campaign_root / (phase + '-plan.json')
        plan = json.loads(plan_path.read_text())
        plan_hash = hashlib.sha256(plan_path.read_bytes()).hexdigest()
        path = args.campaign_root / (phase + '-runs/observations.json')
        rows = json.loads(path.read_text()) if path.exists() else []
        if any(row['plan_sha256'] != plan_hash for row in rows):
            raise ValueError('Plan hash mismatch: ' + phase)
        hosts = {str(p): json.loads(p.read_text()) for p in args.campaign_root.glob(phase + '-host*.json')}
        data['phases'][phase] = dict(plan=plan, plan_sha256=plan_hash, host_states=hosts,
                                     observations_path=str(path))
        for row in rows:
            observation = {key: value for key, value in row.items() if key != 'report'}
            observation['measurement'] = row['report']['measurement']
            observation['browser'] = row['report']['browser']
            observation['renderer'] = row['report']['renderer']
            observation['shared_audio'] = row['report']['shared_audio']
            observation['report_sha256'] = hashlib.sha256((Path(row['directory']) / 'profile/report.json').read_bytes()).hexdigest()
            data['observations'].append(observation)
        for experiment in plan['experiments']:
            for game in experiment['games']:
                subset = [row for row in rows if row['experiment'] == experiment['name'] and row['game'] == game]
                expected = sum(map(len, plan['panels']))
                data['comparisons'].append(dict(phase=phase, experiment=experiment['name'], game=game,
                    original_report=experiment['report'], **summarize(subset, expected)))
    complete = [row for row in data['comparisons'] if row['complete']]
    data['complete'] = len(complete) == len(data['comparisons'])
    data['valid_observations'] = sum(row['valid'] for row in data['comparisons'])
    data['expected_observations'] = sum(row['expected'] for row in data['comparisons'])
    data['invalid_observations'] = sum(row['invalid'] for row in data['comparisons'])
    args.output_stem.with_suffix('.json').write_text(json.dumps(data, indent=2) + '\n')
    lines = ['# Controlled optimization comparisons', '',
        f"Status: {'complete' if data['complete'] else 'in progress'}. "
        f"{len(complete)}/{len(data['comparisons'])} game comparisons complete; "
        f"{data['valid_observations']}/{data['expected_observations']} valid observations and "
        f"{data['invalid_observations']} retained invalid observations.", '',
        'CPU and wall columns show throughput change: positive is faster. Native',
        'instruction change is candidate/control minus one: negative is less work.',
        'Each completed comparison has four fresh observations per variant, in',
        'ABBA then BAAB order. Every valid observation is included. The paired range',
        'shows all four adjacent CPU comparisons; it is not a confidence interval.',
        'Four favorable pairs alone do not prove a small gain generalizes.', '',
        'See [method and controls](CONTROLLED_BENCHMARKS.md) and',
        '[scope and interpretation](CONTROLLED_REASSESSMENT.md). Frozen historical',
        'comparisons measure their original configurations; gains are not additive',
        'and do not establish the same effect on the current production branch.', '',
        '| # | Experiment | Game | CPU throughput | Wall throughput | Native instructions | Paired CPU range | Faster pairs | Invalid |',
        '| ---: | --- | --- | ---: | ---: | ---: | --- | ---: | ---: |']
    for index, row in enumerate(complete, 1):
        pairs = row['paired_cpu_throughput_changes_percent']
        game = 'Snakes' if row['game'] == 'standard' else 'Sky Force'
        lines.append(f"| {index} | {row['experiment']} | {game} | {row['cpu_throughput_change_percent']:+.2f}% | "
            f"{row['wall_throughput_change_percent']:+.2f}% | {row['native_instruction_change_percent']:+.2f}% | "
            f"{min(pairs):+.2f}% to {max(pairs):+.2f}% | {row['favorable_pairs']}/{len(pairs)} | {row['invalid']} |")
    lines += ['', 'All observations, clock checks, errors, exact plans, settings, hashes and',
              'absolute evidence paths are retained in the companion JSON. Raw scheduler',
              'and browser reports remain in the campaign directory.', '',
              '## Remaining comparisons', '']
    for phase in args.phases:
        pending = [row for row in data['comparisons'] if row['phase'] == phase and not row['complete']]
        if pending:
            names = ', '.join(f"{row['experiment']}/{row['game']} ({row['valid']}/{row['expected']})" for row in pending)
            lines.append(f'- {phase}: {names}.')
    if data['complete']:
        lines.append('None.')
    args.output_stem.with_suffix('.md').write_text('\n'.join(lines) + '\n')
    print(f"{len(complete)}/{len(data['comparisons'])} comparisons, "
          f"{data['valid_observations']}/{data['expected_observations']} valid observations")


if __name__ == '__main__':
    main()
