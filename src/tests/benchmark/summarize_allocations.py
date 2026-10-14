#!/usr/bin/env python3
"""Attribute diagnostic WASM allocator lifetimes and Linux browser residency."""
import argparse
import bisect
import collections
import hashlib
import json
from pathlib import Path
import re

from native_attribution import load_source_map, module_info


def decode_stack(stack, module, offsets, anchors):
    frames = []
    for line in stack.splitlines():
        match = re.search(r'wasm-function\[(\d+)\]:(0x[0-9a-f]+)', line)
        if not match:
            continue
        function = module['functions'].get(int(match[1]))
        pc = int(match[2], 16)
        if function and not function['start'] <= pc < function['start'] + function['size']:
            raise ValueError('Allocation stack does not match the supplied WASM artifact')
        index = bisect.bisect_right(offsets, pc) - 1
        anchor = anchors[index] if index >= 0 else None
        frames.append({'function': function['name'] if function else None,
                       'wasm_offset': pc,
                       'source': list(anchor[1:]) if anchor and anchor[1] else None})
    return frames


def subsystem(frames, stack, group, allocation, interpreter):
    sources = [f['source'][0] for f in frames if f['source']]
    names = [f['function'] or '' for f in frames]
    project_sources = [s for s in sources if 'src/emu/' in s and 'wasm/allocation_profile.cpp' not in s]
    nearest = project_sources[0] if project_sources else ''
    if 'mem/src/model/multiple/process.cpp' in nearest:
        return 'Process arenas'
    if any('mem/src/model/multiple/chunk.cpp' in s for s in sources) or any('multiple_mem_model_chunk' in n for n in names):
        return 'Guest chunk backing'
    if any('wasmfs::' in n or 'wasmfs' in n.lower() for n in names):
        return 'WASMFS file storage'
    if interpreter and any(a['group'] == group['id'] and a['requested'] == interpreter['state_size'] for a in allocation.get('large_allocations', [])):
        return 'ARM interpreter state'
    if any('ARMul_State' in n for n in names):
        return 'ARM interpreter state'
    if any('map_file' in n for n in names):
        return 'ROM and mapped files'
    if any('/cpu/' in s for s in sources) or any('aot::' in n for n in names):
        return 'ARM compiler and code cache'
    if any('pthread' in n for n in names) or 'allocateMemory' in stack or 'getNewWorker' in stack:
        return 'Worker stacks and thread storage'
    if any('/loader/' in s for s in sources) or any('load_rom' in n for n in names):
        return 'ROM and executable loading'
    if any('/drivers/' in s for s in sources):
        return 'Graphics and audio drivers'
    if any('/services/' in s for s in sources):
        return 'Emulated OS services'
    return 'Other large allocations'


def mapping_summary(file):
    rows = []
    current = None
    for line in file.read_text().splitlines():
        match = re.match(r'^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+\S+\s+\S+\s+\d+\s*(.*)$', line)
        if match:
            current = {'begin': match[1], 'end': match[2], 'permissions': match[3], 'name': match[4]}
            rows.append(current)
        elif current and (field := re.match(r'^(Size|Pss|Rss|Private_Dirty|Private_Clean|Shared_Dirty|Shared_Clean):\s+(\d+) kB', line)):
            current[field[1] + '_KiB'] = int(field[2])
    categories = collections.Counter()
    for row in rows:
        if row['name'].startswith('/'):
            category = 'File-backed mappings'
        elif 'x' in row['permissions']:
            category = 'Anonymous executable mappings (JIT and trampolines)'
        elif row.get('Size_KiB', 0) >= 1024 * 1024 and 'w' in row['permissions']:
            category = 'Large writable anonymous mappings (inspect before identifying as WASM)'
        elif row['permissions'].startswith('---'):
            category = 'Inaccessible reservations'
        else:
            category = 'Other anonymous mappings'
        categories[category] += row.get('Pss_KiB', 0)
    return {'file': str(file), 'pss_KiB': sum(r.get('Pss_KiB', 0) for r in rows),
            'categories_pss_KiB': dict(categories),
            'largest_mappings': sorted(rows, key=lambda r: r.get('Pss_KiB', 0), reverse=True)[:15]}


def analyze(capture, wasm, source_map):
    module = module_info(wasm)
    offsets, anchors = load_source_map(source_map)
    stages = []
    for file in sorted(capture.glob('*.allocations.json')):
        raw = json.loads(file.read_text())
        allocation = raw['allocation']
        if allocation['dropped'] or allocation['unknown_frees']:
            raise ValueError(f'Lost lifetime coverage: {file}')
        groups = []
        totals = collections.Counter()
        for group in allocation['groups']:
            frames = decode_stack(group['stack'], module, offsets, anchors)
            owner = subsystem(frames, group['stack'], group, allocation, raw.get('interpreter')) if group['id'] else 'Allocations smaller than 64 KiB (unwound coverage limit)'
            totals[owner] += group['live_bytes']
            residency = raw.get('residency', {}).get('allocations', [])
            resident = sum(a['whole_page_resident_bytes'] for a in residency if a['group'] == group['id'])
            groups.append({**group, 'subsystem': owner, 'frames': frames,
                           'whole_page_resident_bytes': resident if residency and group['id'] else None})
        assert sum(totals.values()) == allocation['live_bytes']
        stages.append({'name': raw['name'], 'guest_us': raw['guest'], 'host_ms': raw['ms'],
                       'browserPssKiB': raw['browserPssKiB'], 'wasmCapacity': raw['wasmCapacity'],
                       'memory': raw['memory'],
                       'allocator': {k: v for k, v in allocation.items() if k not in ('groups', 'large_allocations')},
                       'interpreter': raw.get('interpreter'),
                       'residency': {k: v for k, v in raw.get('residency', {}).items() if k != 'allocations'},
                       # dlmalloc has four extra allocator bytes per live block
                       # in this SDK. Concurrent allocator calls can make the
                       # two observations differ; retain the residual explicitly.
                       'allocator_reconciliation_bytes': allocation['mallinfo_allocated_bytes'] - allocation['live_bytes'] - 4 * allocation['live_count'],
                       'subsystem_live_bytes': dict(totals),
                       'subsystem_whole_page_resident_bytes': {
                           owner: (sum(g['whole_page_resident_bytes'] or 0 for g in groups if g['subsystem'] == owner)
                                   if any(g['whole_page_resident_bytes'] is not None for g in groups if g['subsystem'] == owner) else None)
                           for owner in totals} if raw.get('residency', {}).get('allocations') else None,
                       'groups': sorted(groups, key=lambda g: g['live_bytes'], reverse=True)})
    stages.sort(key=lambda s: s['host_ms'])
    samples_file = capture / 'browser-memory-samples.json'
    samples = json.loads(samples_file.read_text()) if samples_file.exists() else []
    result = {'wasm_sha256': module['sha256'], 'source_map_sha256': hashlib.sha256(source_map.read_bytes()).hexdigest(), 'stages': stages,
              'browser_sampled_peak': max(samples, key=lambda s: s['pssKiB']) if samples else None,
              'mapping_snapshots': [mapping_summary(file) for file in sorted(capture.glob('*.smaps'))]}
    (capture / 'allocation-attribution.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--wasm', type=Path, required=True)
    parser.add_argument('--map', dest='source_map', type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.capture, args.wasm, args.source_map)
    last = result['stages'][-1]
    print(json.dumps({k: last[k] for k in ('name', 'browserPssKiB', 'wasmCapacity', 'memory', 'allocator', 'allocator_reconciliation_bytes', 'subsystem_live_bytes')}, indent=2))


if __name__ == '__main__':
    main()
