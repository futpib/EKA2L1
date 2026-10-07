#!/usr/bin/env python3
"""Diagnostic snapshots of the benchmark's own threads outside its timed window.

Optional user-space hardware counters count without sampling.
Does not adjust wall timings. Optional affinity controls reserve a worker CPU.
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
import threading
import time


class HardwareCounters:
    """User-only counting events for existing benchmark threads on Linux x86-64.

    No sampling, inheritance, scheduling changes, or system-wide attachment.
    Counters are independent; raw enabled/running times expose multiplexing.
    """
    def __init__(self, threads, reference_cycles=False):
        self.events = []
        self.result = dict(events={}, errors=[], begin_ns=time.monotonic_ns())
        if platform.system() != 'Linux' or platform.machine() != 'x86_64':
            raise RuntimeError('Hardware counters currently require Linux x86-64')
        libc = ctypes.CDLL(None, use_errno=True)
        libc.syscall.restype = ctypes.c_long
        for key, thread in threads.items():
            events = [(0, 'user_cycles'), (1, 'user_instructions')]
            if reference_cycles:
                events.append((9, 'user_reference_cycles'))
            for config, name in events:
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
                    allowed_cpus=status['Cpus_allowed_list'].strip(), last_cpu=int(stat[36]),
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
    p.add_argument('--start-us', type=int, default=78000000)
    p.add_argument('--end-us', type=int, default=96000000)
    p.add_argument('--capture-mode', type=int, choices=[0, 1, 2], default=2)
    p.add_argument('--hardware-counters', action='store_true',
                   help='Count user cycles/instructions on existing benchmark threads (diagnostic only)')
    p.add_argument('--no-start-gate', action='store_true',
                   help='Attach at the ordinary warmup message without pausing the browser; misses attachment interval')
    p.add_argument('--worker-cpu', type=int,
                   help='Pin the busiest warmup DedicatedWorker to this logical CPU at the start gate')
    p.add_argument('--support-cpus', type=str,
                   help='Comma-separated CPU numbers inherited by other benchmark threads')
    p.add_argument('--reference-mhz', type=float,
                   help='Invariant reference-counter frequency; record worker frequency every 250 ms')
    p.add_argument('--harness', type=Path,
                   help='Directory containing a historical profile.ts matching the frozen builds')
    args = p.parse_args()
    def interrupted(number, frame):
        raise KeyboardInterrupt(f'Signal {number}')
    signal.signal(signal.SIGTERM, interrupted)
    if not 0 <= args.start_us < args.end_us <= 120000000:
        p.error('Expected 0 <= start-us < end-us <= 120000000')
    if args.worker_cpu is not None and args.no_start_gate:
        p.error('Worker affinity requires the start gate')
    if args.reference_mhz and (not args.hardware_counters or args.worker_cpu is None):
        p.error('Frequency monitoring requires hardware counters and worker affinity')
    support_cpus = set(map(int, args.support_cpus.split(','))) if args.support_cpus else None
    if support_cpus is not None and (not support_cpus or args.worker_cpu in support_cpus):
        p.error('Support CPUs must be nonempty and exclude the guest CPU')
    if support_cpus is not None:
        os.sched_setaffinity(0, support_cpus)
    output = args.output.resolve()
    output.mkdir()
    gate = output / 'resume'
    env = dict(os.environ, EKA2L1_WASM_BUILD_DIR=str(args.build.resolve()),
               EKA2L1_BENCHMARK_AOT='5', EKA2L1_GPU='hardware',
               EKA2L1_PROFILE_DETAIL='0', EKA2L1_PROFILE_START_US=str(args.start_us),
               EKA2L1_SHARED_AUDIO=os.environ.get('EKA2L1_SHARED_AUDIO', '1'), PROFILE_GATE=str(gate))
    for key in ('EKA2L1_AOT_VERIFY', 'EKA2L1_GUEST_PROFILE', 'EKA2L1_AOT_DIAGNOSTICS',
                'EKA2L1_V8_FLAGS', 'EKA2L1_V8_DUMP', 'EKA2L1_LONG_MONITOR',
                'EKA2L1_MONITOR_CPU_START_US', 'EKA2L1_CAPTURE_MODULES',
                'EKA2L1_COMPILE_CENSUS'):
        env.pop(key, None)
    if args.no_start_gate:
        env.pop('PROFILE_GATE', None)
    command = ['node', 'profile.ts', str(args.assets.resolve()),
               str(output / 'profile'), str(args.capture_mode), '0', str(args.end_us)]
    if support_cpus is not None:
        command = ['taskset', '-c', ','.join(map(str, sorted(support_cpus))), *command]
    proc = subprocess.Popen(command, cwd=args.harness or args.repo / 'src/tests/wasm', env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, bufsize=1, start_new_session=True)
    counters = hardware = None
    affinity = None
    clock_samples = []
    clock_stop = threading.Event()
    clock_thread = None
    try:
        before = None
        warmup_received_ns = None
        if not args.no_start_gate:
            deadline = time.monotonic() + 1800
            while not Path(str(gate) + '.ready').exists():
                if proc.poll() is not None:
                    raise RuntimeError(proc.stdout.read())
                if time.monotonic() > deadline:
                    raise TimeoutError('Benchmark did not reach its start gate')
                time.sleep(.1)
            before = snapshot(proc.pid)
            if args.worker_cpu is not None:
                workers = [(k, t) for k, t in before['threads'].items() if t['name'] == 'DedicatedWorker']
                if not workers:
                    raise RuntimeError('No DedicatedWorker at the start gate')
                worker_key, worker = max(workers, key=lambda item: item[1]['runtime_ns'])
                old_affinity = sorted(os.sched_getaffinity(worker['tid']))
                os.sched_setaffinity(worker['tid'], {args.worker_cpu})
                affinity = dict(key=worker_key, tid=worker['tid'], pid=worker['pid'],
                    start_ticks=worker['start_ticks'], before=old_affinity,
                    after=sorted(os.sched_getaffinity(worker['tid'])), support_cpus=sorted(support_cpus or []))
            if args.hardware_counters:
                counters = HardwareCounters(before['threads'], bool(args.reference_mhz))
                counters.start()
                if args.reference_mhz:
                    fds = {name: fd for key, name, fd in counters.events if key == worker_key}
                    if not {'user_cycles', 'user_reference_cycles'} <= fds.keys():
                        raise RuntimeError('Guest worker frequency counters unavailable')
                    def sample_clock():
                        while not clock_stop.is_set():
                            row = dict(monotonic_ns=time.monotonic_ns())
                            try:
                                for name, fd in fds.items():
                                    row[name] = list(struct.unpack('=QQQ', os.read(fd, 24)))
                                row['affinity'] = sorted(os.sched_getaffinity(worker['tid']))
                                row['monitor_affinity'] = sorted(os.sched_getaffinity(0))
                                policy = Path(f'/sys/devices/system/cpu/cpufreq/policy{args.worker_cpu}')
                                row['policy'] = {name: (policy / name).read_text().strip() for name in
                                                 ('scaling_governor', 'scaling_min_freq', 'scaling_max_freq')}
                                platform = Path('/sys/firmware/acpi/platform_profile')
                                if platform.exists():
                                    row['platform_profile'] = platform.read_text().strip()
                                row['cgroup_cpus'] = {str(p): p.read_text().strip() for p in
                                    Path('/sys/fs/cgroup').glob('*/cpuset.cpus.effective')}
                                row['cpu_ticks'] = {parts[0]: list(map(int, parts[1:]))
                                    for line in Path('/proc/stat').read_text().splitlines()
                                    if (parts := line.split()) and parts[0].startswith('cpu') and parts[0] != 'cpu'}
                                for name in ('core_throttle_count', 'package_throttle_count'):
                                    path = Path(f'/sys/devices/system/cpu/cpu{args.worker_cpu}/thermal_throttle/{name}')
                                    if path.exists():
                                        row[name] = int(path.read_text())
                            except (OSError, ValueError) as error:
                                row['error'] = str(error)
                            clock_samples.append(row)
                            clock_stop.wait(.25)
                    clock_thread = threading.Thread(target=sample_clock, daemon=True)
                    clock_thread.start()
            gate.write_text('resume\n')
        after = measurement = None
        with (output / 'run.log').open('w') as log:
            for line in proc.stdout:
                log.write(line)
                if args.no_start_gate and line.startswith('Warmup '):
                    if before is not None:
                        raise RuntimeError('Duplicate warmup message')
                    warmup_received_ns = time.monotonic_ns()
                    before = snapshot(proc.pid)
                    if args.hardware_counters:
                        counters = HardwareCounters(before['threads'])
                        counters.start()
                try:
                    value = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict) and 'wall_seconds' in value:
                    if before is None:
                        raise RuntimeError('Measurement arrived before diagnostic attachment')
                    if measurement is not None:
                        raise RuntimeError('Duplicate measurement line')
                    if counters is not None:
                        clock_stop.set()
                        if clock_thread is not None:
                            clock_thread.join()
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
        result = dict(start_gate=not args.no_start_gate, warmup_received_ns=warmup_received_ns,
            scope='Only descendants of the spawned benchmark; before/after snapshots',
            limits='Snapshots bracket the window approximately; exclude exited/new threads. '
                   'A busy thread is not automatically identified as the guest CPU. '
                   'Disabled scheduler statistics mean runnable wait is unavailable, not zero. '
                   'CPU runtime versus wall time alone cannot identify the kind of wait. '
                   'Optional counters exclude kernel/hypervisor and new threads; they perturb execution. '
                   'Independent enable/disable calls approximately bracket the window. '
                   'Enabled/running times expose multiplexing; scaled counts are estimates. '
                   'Without the start gate, snapshots and attachment occur after the warmup message; '
                   'the uncounted beginning and stdout delivery delay are not measured exactly. '
                   'warmup_received_ns and counter enable times bound local attachment work only. '
                   'Do not subtract scheduler wait from reported wall time or discard runs.',
            measurement=measurement, clock_ticks_per_second=os.sysconf('SC_CLK_TCK'),
            runnable_wait_available=wait_available,
            before=before, after=after, thread_deltas=deltas, process_deltas=process_deltas)
        if hardware is not None:
            result['hardware_counters'] = hardware
        if affinity is not None:
            result['worker_affinity'] = affinity
            result['clock_samples'] = clock_samples
            result['reference_mhz'] = args.reference_mhz
        (output / 'scheduler.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(dict(wall_seconds=measurement['wall_seconds'],
                              busiest_threads=deltas[:5]), indent=2))
    finally:
        clock_stop.set()
        if clock_thread is not None:
            clock_thread.join()
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
