#!/usr/bin/env python3
"""Sample userspace instruction pointers using Linux perf, without guest counters.

The stdin protocol starts after the READY response and stops on a line or EOF.
Timestamps and JIT records use CLOCK_MONOTONIC. All losses/errors are retained.
"""
import argparse, collections, ctypes, json, mmap, os, pathlib, platform, select, struct, sys, time


def open_event(tid, period):
    attr = bytearray(128)
    struct.pack_into('<IIQQQ', attr, 0, 1, 128, 1, period, 1 | 2 | 4)
    # disabled, exclude kernel/hypervisor, use_clockid
    struct.pack_into('<Q', attr, 40, 1 | (1 << 5) | (1 << 6) | (1 << 25))
    struct.pack_into('<I', attr, 48, 1)
    struct.pack_into('<i', attr, 92, 1)
    libc = ctypes.CDLL(None, use_errno=True)
    data = (ctypes.c_char * len(attr)).from_buffer(attr)
    fd = libc.syscall(298, ctypes.byref(data), tid, -1, -1, 8)
    if fd < 0: raise OSError(ctypes.get_errno(), os.strerror(ctypes.get_errno()))
    try:
        # A browser has many threads. Eight data pages hold >1000 samples per
        # thread between 50ms drains without exhausting perf_event_mlock_kb.
        ring = mmap.mmap(fd, mmap.PAGESIZE * 9, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
    except Exception:
        os.close(fd)
        raise
    return fd, ring


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('pids', nargs='+', type=int)
    parser.add_argument('--period-us', type=int, default=1000)
    args = parser.parse_args()
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        parser.error('This perf syscall/ring adapter requires Linux x86-64')
    if not 100 <= args.period_us <= 100000: parser.error('period must be 100..100000 us')
    import fcntl
    events, samples, losses, errors = [], [], [], []
    records = collections.Counter()
    try:
        for pid in args.pids:
            for task in pathlib.Path(f'/proc/{pid}/task').iterdir():
                tid = int(task.name)
                try:
                    fd, ring = open_event(tid, args.period_us * 1000)
                    events.append((tid, fd, ring))
                except OSError as error:
                    errors.append({'tid': tid, 'error': str(error)})
        if not events or errors:
            raise RuntimeError(f'Incomplete sampling setup: {errors}')
        def drain():
            for tid, _, ring in events:
                head, tail, offset, size = struct.unpack_from('<QQQQ', ring, 1024)
                if head - tail > size: raise RuntimeError(f'Overwritten perf ring: {tid}')
                while tail < head:
                    start = tail % size
                    def take(n):
                        return bytes(ring[offset+start:offset+min(start+n,size)]) + (bytes(ring[offset:offset+start+n-size]) if start+n>size else b'')
                    kind, misc, length = struct.unpack('<IHH', take(8))
                    if length < 8 or tail + length > head: raise RuntimeError('Invalid perf record')
                    record = take(length)
                    records[kind] += 1
                    if kind == 9:
                        ip, pid, sample_tid, timestamp = struct.unpack_from('<QIIQ', record, 8)
                        samples.append([timestamp, pid, sample_tid, ip])
                    elif kind == 2:
                        losses.append({'tid': tid, 'lost': struct.unpack_from('<Q', record, 16)[0]})
                    elif kind == 13:
                        losses.append({'tid': tid, 'lost': struct.unpack_from('<Q', record, 8)[0]})
                    elif kind == 5:
                        errors.append({'tid': tid, 'error': 'Kernel throttled sampling'})
                    tail += length
                struct.pack_into('<Q', ring, 1032, tail)
        start = time.monotonic_ns()
        for _, fd, _ in events: fcntl.ioctl(fd, 0x2400, 0)
        print('READY', flush=True)
        while not select.select([sys.stdin], [], [], 0.05)[0]: drain()
        for _, fd, _ in events: fcntl.ioctl(fd, 0x2401, 0)
        end = time.monotonic_ns()
        drain()
        initial_tids = {x[0] for x in events}
        end_tids = {int(task.name) for pid in args.pids for task in pathlib.Path(f'/proc/{pid}/task').iterdir()}
        args.output.write_text(json.dumps({'clock': 'CLOCK_MONOTONIC', 'event': 'task-clock:u',
            'period_us': args.period_us, 'start_ns': start, 'end_ns': end,
            'scope': 'Renderer threads present at start; new threads are not sampled',
            'tids': sorted(initial_tids), 'new_tids': sorted(end_tids-initial_tids),
            'missing_tids': sorted(initial_tids-end_tids), 'record_types': dict(records),
            'samples': samples, 'losses': losses, 'errors': errors}))
        if losses or errors: raise RuntimeError(f'Incomplete samples: losses={losses}, errors={errors}')
    finally:
        for _, fd, ring in events: ring.close(); os.close(fd)


if __name__ == '__main__': main()
