#!/usr/bin/env python3
"""Reconcile a browser memory capture without treating samples as resident bytes."""

import argparse
import collections
import json
import re
import subprocess
from pathlib import Path


HEADER = re.compile(r'^([0-9a-f]+)-([0-9a-f]+)\s+([rwxps-]{4})\s+\S+\s+\S+\s+\d+\s*(.*)$', re.M)
PSS = re.compile(r'^Pss:\s+(\d+) kB', re.M)
ADDRESS = re.compile(r'^0x[0-9a-fA-F]+')
ALLOCATOR_FRAMES = ('SamplingHeapProfiler', 'PoissonAllocationSampler',
                    'partition_alloc::PartitionRoot', 'GlibcMallocHook',
                    'GlibcReallocHook', 'operator new', 'posix_memalign')


def mapping_owner(name, permissions, mapping_bytes, wasm_capacity_bytes):
    if name == '[anon:partition_alloc]':
        return 'Chromium PartitionAlloc mappings'
    if (name == '[anon:v8-sandbox]' and 'w' in permissions
            and mapping_bytes == wasm_capacity_bytes):
        return 'WASM linear memory backing'
    if name == '[anon:v8]' and 'x' in permissions:
        return 'V8 executable code and trampolines'
    if name == '[anon:v8-sandbox]':
        return 'Other V8 sandbox mappings'
    if name == '[anon:v8]':
        return 'Other V8 mappings'
    if name.startswith('/'):
        return 'File-backed mappings'
    return 'Other anonymous or shared mappings'


def summarize_smaps(file, wasm_capacity_bytes):
    source = file.read_text()
    headers = list(HEADER.finditer(source))
    owners = collections.Counter()
    for index, header in enumerate(headers):
        end = headers[index + 1].start() if index + 1 < len(headers) else len(source)
        match = PSS.search(source, header.end(), end)
        pss_kib = int(match[1]) if match else 0
        mapping_bytes = int(header[2], 16) - int(header[1], 16)
        owners[mapping_owner(header[4], header[3], mapping_bytes, wasm_capacity_bytes)] += pss_kib
    return {'pss_kib': sum(owners.values()), 'mapping_owners_kib': dict(owners)}


def chromium_symbols(samples, debug_file):
    modules = samples['modules']
    chromium = next((module for module in modules if 'chromium' in module['name'].lower()
                     or 'chrome_profile' in module['name'].lower()), None)
    if not chromium:
        raise ValueError('No Chromium module in native allocation profile')
    build_id = re.search(r'Build ID: ([0-9a-f]+)', subprocess.check_output(
        ['readelf', '-n', str(debug_file)], text=True, stderr=subprocess.DEVNULL))
    if not build_id or not chromium['uuid'].lower().startswith(build_id[1]):
        raise ValueError('Chromium symbol file does not match the captured browser')
    base = int(chromium['baseAddress'], 16)
    limit = base + chromium['size']
    offsets = sorted({address - base for sample in samples['samples'] for frame in sample['stack']
                      if (match := ADDRESS.match(frame)) and base <= (address := int(match[0], 16)) < limit})
    result = subprocess.check_output(['addr2line', '-f', '-C', '-e', str(debug_file)],
                                     input=''.join(f'{offset:#x}\n' for offset in offsets), text=True)
    lines = result.splitlines()
    if len(lines) != 2 * len(offsets):
        raise ValueError('Chromium symbolizer returned an unexpected number of lines')
    return chromium, {offset: lines[2 * index] for index, offset in enumerate(offsets)}


def summarize_native(file, debug_file):
    if not file.exists():
        return None
    profile = json.loads(file.read_text())
    module, symbols = chromium_symbols(profile, debug_file)
    base = int(module['baseAddress'], 16)
    limit = base + module['size']
    by_site = collections.Counter()
    by_caller = collections.Counter()
    unresolved = 0
    for sample in profile['samples']:
        stack = []
        for frame in sample['stack']:
            match = ADDRESS.match(frame)
            if not match:
                continue
            address = int(match[0], 16)
            name = symbols.get(address - base) if base <= address < limit else frame
            if name and name != '??':
                stack.append(name)
        owners = [frame for frame in stack if not any(marker in frame for marker in ALLOCATOR_FRAMES)]
        if not owners:
            unresolved += sample['total']
        by_site[owners[0] if owners else '(unresolved)'] += sample['total']
        by_caller[' -> '.join(owners[:3]) if owners else '(unresolved)'] += sample['total']
    return {'samples': len(profile['samples']), 'sampled_estimate_bytes': sum(s['total'] for s in profile['samples']),
            'chromium_build_id': module['uuid'], 'symbolized_chromium_addresses': len(symbols),
            'unresolved_estimate_bytes': unresolved,
            'top_sites': [{'site': name, 'sampled_estimate_bytes': size} for name, size in by_site.most_common(30)],
            'top_callers': [{'path': name, 'sampled_estimate_bytes': size} for name, size in by_caller.most_common(30)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--chromium-debug', type=Path, required=True)
    parser.add_argument('--label', help='Measured route label; defaults to the newest route report')
    args = parser.parse_args()
    capture = args.capture
    candidates = []
    for file in capture.glob('*.json'):
        if file.stat().st_size > 1024 * 1024:
            continue
        try:
            data = json.loads(file.read_text())
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if isinstance(data, dict) and {'game', 'browserMemory', 'browserPssKiB', 'label'} <= data.keys():
            candidates.append(file)
    candidates.sort(key=lambda file: file.stat().st_mtime)
    label = args.label or (candidates[-1].stem if candidates else None)
    if not label:
        raise ValueError('No route report found; pass --label for an existing route')
    report = json.loads((capture / f'{label}.json').read_text())
    if report['errors'] or (capture / 'failure.json').exists():
        raise ValueError('The game route did not complete without errors')
    processes = []
    for process in report['browserMemory']:
        file = capture / f'{label}-{process["id"]}.smaps'
        processes.append({**process, 'smaps': summarize_smaps(file, report['wasmCapacity']) if file.exists() else None})
    if any(process['smaps'] is None for process in processes):
        raise ValueError('At least one Chrome process lacks a smaps capture')
    renderer = max((process for process in processes if process['type'] == 'renderer'), key=lambda row: row['pssKiB'])
    jit_file = capture / f'{label}.jit-code-residency.json'
    if not jit_file.exists():
        raise ValueError('The game renderer lacks its executable-page attribution')
    jit_code = json.loads(jit_file.read_text())
    heap_file = capture / 'chrome-memory-summary.json'
    if not heap_file.exists():
        raise ValueError('Chrome allocation-sampling results are missing')
    heap = json.loads(heap_file.read_text())
    if any(target['errors'] or 'v8' not in target for target in heap):
        raise ValueError('One or more Chrome isolate heap samples failed')
    all_mappings = collections.Counter()
    for process in processes:
        if process['smaps']:
            all_mappings.update(process['smaps']['mapping_owners_kib'])
    if all_mappings['WASM linear memory backing'] == 0:
        raise ValueError('No mapping matches the captured WASM memory capacity')
    native = summarize_native(capture / 'page.native-memory.json', args.chromium_debug)
    if native is None:
        raise ValueError('Native allocation samples are missing')
    result = {'game': report['game'], 'label': label, 'wasm_url': report['wasm'],
              'browser': report['browser'], 'browser_pss_kib': report['browserPssKiB'],
              'wasm_capacity_bytes': report['wasmCapacity'], 'route_completed_no_errors': not report['errors'],
              'guest_errors': report['errors'], 'processes': processes,
              'renderer_pid': renderer['id'],
              'renderer_mapping_total_kib': renderer['smaps']['pss_kib'] if renderer['smaps'] else None,
              'browser_mapping_owners_kib': dict(all_mappings),
              'jit_code_residency': jit_code,
              'v8_heap_samples': {'isolates': len(heap),
                                  'sampled_estimate_bytes': sum(target['v8']['sampled_bytes'] for target in heap),
                                  'by_isolate': {target['target']: target['v8']['sampled_bytes'] for target in heap}},
              'missing_process_maps_pss_kib': sum(process['pssKiB'] for process in processes if not process['smaps']),
              'process_map_pss_deltas_kib': {str(process['id']): process['smaps']['pss_kib'] - process['pssKiB']
                                             for process in processes if process['smaps']},
              'native_allocation_samples': native}
    total = sum(process['pssKiB'] for process in processes)
    if total != report['browserPssKiB']:
        raise ValueError('Process PSS rows do not reconcile with the route report')
    (capture / 'browser-memory-attribution.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'browser_pss_mib': total / 1024, 'renderer_pss_mib': renderer['pssKiB'] / 1024,
                      'all_process_mapping_mib': {key: round(value / 1024, 2) for key, value in all_mappings.items()},
                      'renderer_mapping_mib': {key: round(value / 1024, 2) for key, value in
                                               renderer['smaps']['mapping_owners_kib'].items()},
                      'jit_code_residency': jit_code,
                      'v8_heap_samples': result['v8_heap_samples'],
                      'native_top_sites': result['native_allocation_samples']['top_sites'][:10]
                      if result['native_allocation_samples'] else None}, indent=2))


if __name__ == '__main__':
    main()
