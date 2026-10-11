#!/usr/bin/env python3
"""Reduce the archived diagnostic trace; no emulator process is needed.

Usage: python reduce.py /path/to/barrier-trace-quiet.trace
The matching -trace.json and -switches.json must be beside the trace.
All times are microseconds unless a field explicitly says otherwise.
"""
import argparse
import collections
import json
from pathlib import Path
import struct


def stats(values):
    values = sorted(values)
    if not values:
        return None
    return dict(n=len(values), mean=sum(values) / len(values),
                median=values[len(values) // 2], min=values[0], max=values[-1],
                p95=values[int(.95 * (len(values) - 1))])


def reduce(path):
    def sibling(suffix):
        return path.with_name(path.stem + suffix)

    meta = json.loads(sibling('-trace.json').read_text())
    raw = path.read_bytes()
    assert meta['count'] <= meta['capacity'], 'Overflow: use a separately labeled prefix analysis'
    assert len(raw) == meta['count'] * 64
    rows = [dict(zip(('host', 'guest', 'kind', 'thread', 'a', 'b', 'c', 'd'), r))
            for r in struct.iter_unpack('<8Q', raw)]
    # Browser timestamps can tie. Preserve recording order at ties, not tuple order.
    rows.sort(key=lambda r: r['host'])
    wasm = any(r['kind'] == 24 for r in rows)
    frames = [r['host'] for r in rows if r['kind'] == (24 if wasm else 23)]
    assert len(frames) > 1
    start, end = frames[0], frames[-1]
    count = len(frames) - 1
    cpu = next(r['thread'] for r in rows if r['kind'] == 2)
    marks = {1: 'loop setup', 2: 'guest run and synchronous services',
             3: 'clock advance and timer work' if wasm else 'post-run bookkeeping',
             4: 'scheduler', 5: 'idle decision and loop return',
             6: 'host idle sleep', 7: 'loop return and input drain'}
    edges = [r for r in rows if r['thread'] == cpu and r['kind'] in marks]

    def clipped(a, b):
        return max(0, min(end, b) - max(start, a))

    phases = collections.Counter()
    for a, b in zip(edges, edges[1:]):
        phases[marks[a['kind']]] += clipped(a['host'], b['host'])
    assert sum(phases.values()) == end - start, 'Incomplete wall-time partition'

    svc = None
    run = None
    svc_time = collections.Counter()
    svc_counts = collections.Counter()
    run_time = collections.Counter()
    unmatched = 0
    for r in rows:
        if r['thread'] != cpu:
            continue
        k = r['kind']
        if k == 2:
            assert run is None
            run = r
        elif k == 3 and run:
            run_time[run['a']] += clipped(run['host'], r['host'])
            run = None
        elif k == 19:
            if svc and start <= svc['host'] < end:
                unmatched += 1
            svc = r
        elif k == 21 and svc:
            assert svc['a'] == r['a']
            svc_time[r['a']] += clipped(svc['host'], r['host'])
            svc_counts[r['a']] += start <= svc['host'] < end
            svc = None
    assert not unmatched, 'SVC early exits or nesting need separate attribution'
    assert sum(run_time.values()) == phases[marks[2]]
    assert sum(svc_time.values()) <= sum(run_time.values())

    # This request-to-delivery view overlaps the worker phases above.
    # It is not another additive wall-time partition.
    pending, delivered = [], []
    args = None
    late = collections.defaultdict(list)
    named_late = collections.defaultdict(list)
    schedules = {}
    delays = collections.defaultdict(list)
    wakes = []
    wake_latency = []
    for r in rows:
        k = r['kind']
        if k == 20:
            args = r
        elif k == 9:
            name = meta['names'][str(r['d'])]
            if start <= r['host'] < end:
                late[name].append(r['guest'] - r['c'])
                named_late[schedules.get((r['a'], r['b'], r['c']), name)].append(r['guest'] - r['c'])
        elif k in (12, 13, 17):
            assert args is not None
            name = ('display pacing' if k == 12 else
                    f'User::After({args["a"]})' if k == 13 else
                    f'RTimer::After({args["c"]})')
            pending.append(dict(name=name, thread=r['a'],
                                object=r['b'] if k == 17 else r['a'],
                                host=r['host'], scheduled=False))
        elif k == 11:
            name = meta['names'][str(r['d'])]
            if name == 'SchedulerWakeUpThread':
                q = next((q for q in reversed(pending)
                          if q['thread'] == r['b'] and not q['scheduled']
                          and not q['name'].startswith('RTimer')), None)
                if q:
                    name = q['name']
                    q['scheduled'] = True
            if start <= r['host'] < end:
                delays[name].append(r['c'] - r['guest'])
            schedules[(r['a'], r['b'], r['c'])] = name
        elif k in (14, 18):
            obj = r['b'] if k == 18 else r['a']
            q = next((q for q in reversed(pending) if q['object'] == obj
                      and ('done' not in q)
                      and q['name'].startswith('RTimer') == (k == 18)), None)
            if q:
                q['done'] = r['host']
                delivered.append(q)
            if r['a'] == 128:
                wakes.append(r)
        elif k == 2 and r['a'] == 128:
            wake_latency.extend(r['host'] - q['host'] for q in wakes
                                if start <= q['host'] <= r['host'] <= end)
            wakes = []
    wait_times = {name: stats(q['done'] - q['host'] for q in delivered
                             if q['name'] == name and start <= q['host'] < q['done'] <= end)
                  for name in sorted({q['name'] for q in delivered})}
    vsync = [r['a'] for r in rows if r['kind'] == 22 and start <= r['host'] < end]

    switches = json.loads(sibling('-switches.json').read_text())
    assert not any(switches[k] for k in ('errors', 'losses', 'new_tids', 'missing_tids'))
    if wasm:
        tid = meta['cpu_time']['busiest_renderer_thread']['tid']
        low = max(a['host_ms'] * 1000 - a['after'] / 1000 for a in meta['anchors'])
        high = min(a['host_ms'] * 1000 - a['before'] / 1000 for a in meta['anchors'])
        assert low <= high
        offset, uncertainty = (low + high) / 2, (high - low) / 2
    else:
        # Native CPU worker inferred from the dominant task-clock sample stream.
        tid = collections.Counter(s[2] for s in switches['samples']).most_common(1)[0][0]
        offset = uncertainty = 0
    timeline = sorted(s for s in switches['switches'] if s[2] == tid)
    on = timeline[0][3]  # An OUT event means it was running immediately before.
    previous = switches['start_ns'] / 1000
    run_us = 0
    for t, _, _, is_out, _ in timeline:
        t /= 1000
        if on:
            run_us += clipped(previous + offset, t + offset)
        previous, on = t, not is_out
    if on:
        run_us += clipped(previous + offset, switches['end_ns'] / 1000 + offset)
    assert 0 <= run_us <= end - start
    return dict(frontend='WASM' if wasm else 'Qt', events=len(rows),
                frame_boundary='presentation callback' if wasm else 'framebuffer submission',
                frame_intervals=count, start_host_us=start, end_host_us=end,
                wall_us=end - start, wall_ms_per_frame=(end - start) / count / 1000,
                exclusive_worker_phases_us=dict(phases),
                exclusive_worker_phases_ms_per_frame={k: v / count / 1000 for k, v in phases.items()},
                cpu_tid=tid, cpu_tid_method='busiest renderer counter' if wasm else 'dominant task-clock samples',
                on_cpu_us=run_us, off_cpu_us=end - start - run_us,
                clock_offset_us=offset, clock_uncertainty_halfwidth_us=uncertainty,
                svc_us={hex(k): v for k, v in svc_time.items()},
                svc_count={hex(k): v for k, v in svc_counts.items()},
                guest_thread_run_us=dict(run_time), request_to_delivery_us=wait_times,
                guest_wake_to_cpu_entry_us=stats(wake_latency),
                timer_lateness_us={k: stats(v) for k, v in late.items()},
                request_kind_lateness_us={k: stats(v) for k, v in named_late.items()},
                scheduled_delay_us={k: stats(v) for k, v in delays.items()},
                vsync_requested_us=stats(vsync), vsync_zero_count=vsync.count(0),
                measurement=meta['measurement'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(reduce(args.trace), indent=2) + '\n'
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end='')
