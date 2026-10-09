#!/usr/bin/env python3
"""Measure equal-work watchdog kernels; run inside fixed_frequency.py isolation."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from scheduler_probe import HardwareCounters


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('test', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--cpu', type=int, default=7)
    parser.add_argument('--mhz', type=float, default=3600)
    parser.add_argument('--reference-mhz', type=float, default=2304)
    args = parser.parse_args()
    args.output.mkdir()
    observations = []
    counters = None
    process = subprocess.Popen(['node', str(args.test.resolve()), '--watchdog-kernels'],
        env={**os.environ, 'EKA2L1_KERNEL_GATES': '1'}, text=True,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        with (args.output / 'test.log').open('w') as log:
            for line in process.stdout:
                log.write(line)
                log.flush()
                if line.startswith('WATCHDOG_KERNEL_KIND '):
                    kind = line.split()[1]
                elif line.startswith('WATCHDOG_KERNEL_BEGIN '):
                    os.sched_setaffinity(process.pid, {args.cpu})
                    stat = Path(f'/proc/{process.pid}/stat').read_text().rsplit(')', 1)[1].split()
                    counters = HardwareCounters({'worker': dict(pid=process.pid,
                        tid=process.pid, start_ticks=int(stat[19]))}, True)
                    counters.start()
                    process.stdin.write('\n')
                    process.stdin.flush()
                elif line.startswith('WATCHDOG_KERNEL_END '):
                    hardware = counters.stop()
                    counters.close()
                    counters = None
                    result = json.loads(line.partition(' ')[2])
                    errors = list(hardware['errors'])
                    events = hardware['events'].get('worker', {})
                    if any(events.get(name, {}).get('running_fraction') != 1 for name in
                           ('user_cycles', 'user_instructions', 'user_reference_cycles')):
                        errors.append('Missing or multiplexed counters')
                    cycles = events.get('user_cycles', {}).get('raw', 0)
                    reference = events.get('user_reference_cycles', {}).get('raw', 0)
                    mhz = cycles / reference * args.reference_mhz if reference else 0
                    if abs(mhz / args.mhz - 1) > .005:
                        errors.append('Mean frequency outside predeclared 0.5 percent tolerance')
                    if os.sched_getaffinity(process.pid) != {args.cpu}:
                        errors.append('Worker affinity changed')
                    observations.append(dict(kind=kind, **result, hardware=hardware,
                        actual_mhz=mhz, valid=not errors, errors=errors))
                    (args.output / 'observations.json').write_text(json.dumps(observations, indent=2))
                    process.stdin.write('r' if errors else '\n')
                    process.stdin.flush()
        code = process.wait()
        if code or sum(r['valid'] for r in observations) != 16:
            raise RuntimeError('Kernel run failed; all observations retained')
    finally:
        if counters:
            counters.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=30)


if __name__ == '__main__':
    main()
