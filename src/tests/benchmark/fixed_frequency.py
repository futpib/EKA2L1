#!/usr/bin/env python3
"""Run a command with equal CPU frequency limits, restoring every policy afterward.

Requires root for sysfs writes. The child runs as the invoking sudo user.
Limits are requests, not measurements; pair with scheduler_probe.py frequency
counters. Normal termination, child failure and handled signals restore policy.
"""
import argparse
import json
import os
from pathlib import Path
import pwd
import signal
import subprocess
import time


def policies():
    return {str(p): {name: (p / name).read_text().strip() for name in
            ('scaling_governor', 'scaling_min_freq', 'scaling_max_freq',
             'energy_performance_preference') if (p / name).exists()}
            for p in sorted(Path('/sys/devices/system/cpu/cpufreq').glob('policy*'))}


def write(path, value):
    Path(path).write_text(str(value))


def set_limits(path, minimum, maximum):
    # Lower the minimum first so both upward and downward transitions are legal.
    write(path / 'scaling_min_freq', min(int((path / 'scaling_min_freq').read_text()), minimum))
    write(path / 'scaling_max_freq', maximum)
    write(path / 'scaling_min_freq', minimum)


def cpu_set(text):
    values = set()
    for part in text.strip().split(','):
        if not part:
            continue
        ends = list(map(int, part.split('-')))
        values.update(range(ends[0], ends[-1] + 1))
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--khz', type=int, required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--isolate-cpus', type=str,
                        help='Reserve these CPUs from other top-level cgroups; run inside ekabench.slice')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if os.geteuid() != 0 or not command or args.khz <= 0:
        parser.error('Requires root, positive --khz, and a command after --')
    if args.state.exists():
        parser.error('State file already exists; refusing to overwrite restoration evidence')
    before = policies()
    if not before:
        raise RuntimeError('No CPU frequency policies')
    for path in before:
        p = Path(path)
        if not int((p / 'cpuinfo_min_freq').read_text()) <= args.khz <= int((p / 'cpuinfo_max_freq').read_text()):
            raise RuntimeError(f'Request outside supported range: {p}')
    record = dict(before=before, target_khz=args.khz, command=command, start=time.time())
    placements = {}
    if args.isolate_cpus:
        if '/ekabench.slice/' not in Path('/proc/self/cgroup').read_text():
            raise RuntimeError('Isolation requires a scope inside ekabench.slice')
        root = Path('/sys/fs/cgroup')
        for pid in (root / 'cgroup.procs').read_text().split():
            try:
                stat = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
                if not int(stat[6]) & 0x00200000:  # PF_KTHREAD; IRQ/kernel work remains a limit.
                    raise RuntimeError(f'Root cgroup contains user process {pid} outside isolation')
            except FileNotFoundError:
                pass
        reserved = cpu_set(args.isolate_cpus)
        allowed = cpu_set((root / 'cpuset.cpus.effective').read_text()) - reserved
        if not reserved or not allowed:
            raise RuntimeError('Need both reserved and support CPUs')
        for path in root.glob('*/cpuset.cpus'):
            if path.parent.name == 'ekabench.slice':
                continue
            original = path.read_text().strip()
            assigned = (cpu_set(original) if original else allowed) & allowed
            if not assigned:
                raise RuntimeError(f'Cannot preserve CPU placement for {path}')
            placements[str(path)] = dict(before=original,
                before_effective=(path.parent / 'cpuset.cpus.effective').read_text().strip(),
                requested=','.join(map(str, sorted(assigned))))
        record['placements'] = placements
    args.state.write_text(json.dumps(record, indent=2) + '\n')
    child = None
    def interrupted(number, frame):
        if child is not None and child.poll() is None:
            os.killpg(child.pid, number)
        raise KeyboardInterrupt(f'Signal {number}')
    for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, interrupted)
    try:
        for path, values in placements.items():
            write(path, values['requested'])
        for path in before:
            write(Path(path) / 'scaling_governor', 'performance')
        for attempt in range(5):
            for path in before:
                set_limits(Path(path), args.khz, args.khz)
            time.sleep(.1)
            if all(int(v['scaling_min_freq']) == args.khz and int(v['scaling_max_freq']) == args.khz
                   for v in policies().values()):
                break
        record['requested'] = policies()
        args.state.write_text(json.dumps(record, indent=2) + '\n')
        if any(int(v['scaling_min_freq']) != args.khz or int(v['scaling_max_freq']) != args.khz
               for v in record['requested'].values()):
            raise RuntimeError('Frequency request did not read back exactly')
        if 'SUDO_UID' in os.environ:
            user = pwd.getpwuid(int(os.environ['SUDO_UID'])).pw_name
            command = ['sudo', '-u', user, '-H', '--', *command]
        child = subprocess.Popen(command, start_new_session=True)
        record['returncode'] = child.wait()
    finally:
        if child is not None and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=45)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
        errors = []
        for path, values in placements.items():
            try:
                write(path, values['before'] or '\n')
                values['after'] = Path(path).read_text().strip()
                if values['after'] != values['before']:
                    errors.append(dict(path=path, error='CPU placement not restored'))
            except OSError as error:
                # Linux 6.12 refuses to clear an explicit mask on a populated
                # cgroup. Restore the same effective CPUs and record that form.
                if error.errno == 28 and not values['before']:
                    write(path, values['before_effective'])
                    values['after'] = Path(path).read_text().strip()
                    values['restored_as_explicit_mask'] = True
                else:
                    errors.append(dict(path=path, error=str(error)))
        for path, values in before.items():
            try:
                p = Path(path)
                set_limits(p, int(values['scaling_min_freq']), int(values['scaling_max_freq']))
                write(p / 'scaling_governor', values['scaling_governor'])
                if 'energy_performance_preference' in values:
                    write(p / 'energy_performance_preference', values['energy_performance_preference'])
            except OSError as error:
                errors.append(dict(path=path, error=str(error)))
        record.update(after=policies(), restoration_errors=errors, end=time.time())
        record['restored'] = record['after'] == before and not errors
        args.state.write_text(json.dumps(record, indent=2) + '\n')
        if not record['restored']:
            raise RuntimeError(f'CPU policy restoration incomplete: {args.state}')
    raise SystemExit(record['returncode'])


if __name__ == '__main__':
    main()
