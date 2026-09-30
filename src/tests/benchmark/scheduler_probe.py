#!/usr/bin/env python3
"""Diagnostic snapshots of the benchmark's own threads outside its timed window.

Optional user-space hardware counters count without sampling.
Does not adjust wall timings or change host scheduling.
Not a substitute for ordinary serial timing controls.
"""
import argparse
import ctypes
import fcntl
import json
import os
from pathlib import Path
import platform
import signal
import struct
import subprocess
import time


class HardwareCounters:
    """User-only counting events for existing benchmark threads on Linux x86-64.

    No sampling, inheritance, scheduling changes, or system-wide attachment.
    Counters are independent; raw enabled/running times expose multiplexing.
    """
    def __init__(self, threads):
        self.events = []
        self.result = dict(events={}, errors=[], begin_ns=time.monotonic_ns())
        if platform.system() != 'Linux' or platform.machine() != 'x86_64':
            raise RuntimeError('Hardware counters currently require Linux x86-64')
        libc = ctypes.CDLL(None, use_errno=True)
        libc.syscall.restype = ctypes.c_long
        for key, thread in threads.items():
            for config, name in ((0, 'user_cycles'), (1, 'user_instructions')):
                fd = None
                try:
                    # perf_event_attr version 0; disabled, exclude kernel/hypervisor.
                    attr = ctypes.create_string_buffer(64)
                    struct.pack_into('=IIQQQQQ', attr, 0, 0, 64, config, 0, 0,
                                     3, 1 | (1 << 5) | (1 << 6))
                    fd = libc.syscall(298, ctypes.byref(attr), thread['tid'], -1, -1, 8)
                    if fd < 0:
                        raise OSError(ctypes.get_errno(), os.strerror(ctypes.get_errno()))
                    stat = Path(f"/proc/{thread['pid']}/task/{thread['tid']}/stat").read_text()
                    if int(stat.rsplit(')', 1)[1].split()[19]) != thread['start_ticks']:
                        raise RuntimeError('Thread identity changed before attachment')
                    self.events.append((key, name, fd))
                    self.result['events'].setdefault(key, dict(start_ticks=thread['start_ticks']))
                except (OSError, RuntimeError) as error:
                    if fd is not None and fd >= 0:
                        os.close(fd)
                    self.result['errors'].append(dict(thread=key, event=name, stage='attach',
                                                      error=str(error)))

    def start(self):
        self.result['enable_begin_ns'] = time.monotonic_ns()
        for key, name, fd in self.events:
            try:
                fcntl.ioctl(fd, 0x2400, 0)  # PERF_EVENT_IOC_ENABLE
            except OSError as error:
                self.result['errors'].append(dict(thread=key, event=name, stage='enable',
                                                  error=str(error)))
        self.result['enable_end_ns'] = time.monotonic_ns()

    def stop(self):
        self.result['disable_begin_ns'] = time.monotonic_ns()
        for key, name, fd in self.events:
            try:
                fcntl.ioctl(fd, 0x2401, 0)  # PERF_EVENT_IOC_DISABLE
            except OSError as error:
                self.result['errors'].append(dict(thread=key, event=name, stage='disable',
                                                  error=str(error)))
        self.result['disable_end_ns'] = time.monotonic_ns()
        for key, name, fd in self.events:
            try:
                raw, enabled, running = struct.unpack('=QQQ', os.read(fd, 24))
                self.result['events'][key][name] = dict(raw=raw, time_enabled_ns=enabled,
                    time_running_ns=running,
                    running_fraction=running / enabled if enabled else None,
                    scaled=raw * enabled / running if running else None)
            except (OSError, struct.error) as error:
                self.result['errors'].append(dict(thread=key, event=name, stage='read',
                                                  error=str(error)))
        return self.result

    def close(self):
        for _, _, fd in self.events:
            os.close(fd)
        self.events.clear()


def snapshot(root_pid):
    start = time.monotonic_ns()
    pending, seen, threads, processes = [root_pid], set(), {}, {}
    while pending:
        pid = pending.pop()
        if pid in seen:
            continue
        seen.add(pid)
        try:
            tasks = list(Path(f'/proc/{pid}/task').iterdir())
            stat = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
            processes[str(pid)] = dict(pid=pid, start_ticks=int(stat[19]),
                user_ticks=int(stat[11]), system_ticks=int(stat[12]))
        except FileNotFoundError:
            continue
        for task in tasks:
            try:
                # Children can have been spawned by a non-main thread.
                pending.extend(map(int, (task / 'children').read_text().split()))
                stat = (task / 'stat').read_text().rsplit(')', 1)[1].split()
                scheduled = list(map(int, (task / 'schedstat').read_text().split()))
                status = dict(line.split(':', 1) for line in (task / 'status').read_text().splitlines())
                threads[f'{pid}:{task.name}'] = dict(
                    pid=pid, tid=int(task.name), name=(task / 'comm').read_text().strip(),
                    start_ticks=int(stat[19]), user_ticks=int(stat[11]),
                    system_ticks=int(stat[12]), runtime_ns=scheduled[0],
                    runnable_wait_ns=scheduled[1], timeslices=scheduled[2],
                    voluntary_switches=int(status['voluntary_ctxt_switches']),
                    involuntary_switches=int(status['nonvoluntary_ctxt_switches']))
            except (FileNotFoundError, ProcessLookupError):
                pass  # Record only tasks that survived their snapshot reads.
    return dict(begin_ns=start, end_ns=time.monotonic_ns(), threads=threads,
                processes=processes,
                schedstats_setting=Path('/proc/sys/kernel/sched_schedstats').read_text().strip())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('assets', type=Path)
    p.add_argument('build', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[3])
    p.add_argument('--hardware-counters', action='store_true',
                   help='Count user cycles/instructions on existing benchmark threads (diagnostic only)')
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir()
    gate = output / 'resume'
    env = dict(os.environ, EKA2L1_WASM_BUILD_DIR=str(args.build.resolve()),
               EKA2L1_BENCHMARK_AOT='5', EKA2L1_GPU='hardware',
               EKA2L1_PROFILE_DETAIL='0', EKA2L1_PROFILE_START_US='78000000',
               EKA2L1_SHARED_AUDIO='1', PROFILE_GATE=str(gate))
    for key in ('EKA2L1_AOT_VERIFY', 'EKA2L1_GUEST_PROFILE', 'EKA2L1_AOT_DIAGNOSTICS',
                'EKA2L1_V8_FLAGS', 'EKA2L1_V8_DUMP', 'EKA2L1_LONG_MONITOR',
                'EKA2L1_MONITOR_CPU_START_US', 'EKA2L1_CAPTURE_MODULES',
                'EKA2L1_COMPILE_CENSUS'):
        env.pop(key, None)
    command = ['node', 'profile.ts', str(args.assets.resolve()),
               str(output / 'profile'), '2', '0', '96000000']
    proc = subprocess.Popen(command, cwd=args.repo / 'src/tests/wasm', env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, bufsize=1, start_new_session=True)
    counters = hardware = None
    try:
        deadline = time.monotonic() + 1800
        while not Path(str(gate) + '.ready').exists():
            if proc.poll() is not None:
                raise RuntimeError(proc.stdout.read())
            if time.monotonic() > deadline:
                raise TimeoutError('Benchmark did not reach its start gate')
            time.sleep(.1)
        before = snapshot(proc.pid)
        if args.hardware_counters:
            counters = HardwareCounters(before['threads'])
            counters.start()
        gate.write_text('resume\n')
        after = measurement = None
        with (output / 'run.log').open('w') as log:
            for line in proc.stdout:
                log.write(line)
                try:
                    value = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict) and 'wall_seconds' in value:
                    if measurement is not None:
                        raise RuntimeError('Duplicate measurement line')
                    if counters is not None:
                        hardware = counters.stop()
                    after, measurement = snapshot(proc.pid), value
        if proc.wait() != 0 or after is None:
            raise RuntimeError('Benchmark failed or omitted its measurement')
        deltas = []
        wait_available = before['schedstats_setting'] == after['schedstats_setting'] == '1'
        for key, last in after['threads'].items():
            first = before['threads'].get(key)
            if first is None or first['start_ticks'] != last['start_ticks']:
                continue
            row = {k: last[k] for k in ('pid', 'tid', 'name')}
            for k in ('runtime_ns', 'runnable_wait_ns', 'timeslices', 'user_ticks', 'system_ticks',
                      'voluntary_switches', 'involuntary_switches'):
                row[k] = last[k] - first[k]
            if not wait_available:
                row['runnable_wait_ns'] = None
            if hardware is not None:
                row['hardware'] = hardware['events'].get(key)
            deltas.append(row)
        deltas.sort(key=lambda x: x['runtime_ns'], reverse=True)
        process_deltas = []
        for key, last in after['processes'].items():
            first = before['processes'].get(key)
            if first is not None and first['start_ticks'] == last['start_ticks']:
                process_deltas.append(dict(pid=last['pid'],
                    user_ticks=last['user_ticks'] - first['user_ticks'],
                    system_ticks=last['system_ticks'] - first['system_ticks']))
        result = dict(scope='Only descendants of the spawned benchmark; before/after snapshots',
            limits='Snapshots bracket the window approximately; exclude exited/new threads. '
                   'A busy thread is not automatically identified as the guest CPU. '
                   'Disabled scheduler statistics mean runnable wait is unavailable, not zero. '
                   'CPU runtime versus wall time alone cannot identify the kind of wait. '
                   'Optional counters exclude kernel/hypervisor and new threads; they perturb execution. '
                   'Independent enable/disable calls approximately bracket the window. '
                   'Enabled/running times expose multiplexing; scaled counts are estimates. '
                   'Do not subtract scheduler wait from reported wall time or discard runs.',
            measurement=measurement, clock_ticks_per_second=os.sysconf('SC_CLK_TCK'),
            runnable_wait_available=wait_available,
            before=before, after=after, thread_deltas=deltas, process_deltas=process_deltas)
        if hardware is not None:
            result['hardware_counters'] = hardware
        (output / 'scheduler.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(dict(wall_seconds=measurement['wall_seconds'],
                              busiest_threads=deltas[:5]), indent=2))
    finally:
        if counters is not None:
            counters.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()


if __name__ == '__main__':
    main()
