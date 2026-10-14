#!/usr/bin/env python3
"""Inspect Chrome allocation profiles with the existing WASM source-map decoder."""
import argparse
import bisect
import hashlib
import json
from pathlib import Path

from native_attribution import load_source_map, module_info


def analyze(capture, wasm, source_map, program):
    offsets, anchors = load_source_map(source_map)
    module = module_info(wasm)
    functions = list(module['functions'].values())
    summaries = json.loads((capture / 'chrome-memory-summary.json').read_text())
    mapped = []
    for target in summaries:
        for node in target.get('v8', {}).get('wasm_frames', []):
            frame = node['callFrame']
            if program not in frame['url']:
                mapped.append({'target': target['target'], 'frame': frame,
                               'scope': 'other module; no matching source map'})
                continue
            offset = frame['columnNumber']
            function = next((f for f in functions if f['start'] <= offset < f['start'] + f['size']), None)
            index = bisect.bisect_right(offsets, offset) - 1
            anchor = anchors[index] if index >= 0 else None
            # Heap sampling frequently reports a js-to-wasm entry wrapper,
            # not the allocating instruction. Never relabel it as a callsite.
            wrapper = frame['functionName'].startswith('js-to-wasm:')
            mapped.append({'target': target['target'], 'frame': frame,
                           'scope': 'entry wrapper, not allocation site' if wrapper else 'reported WASM position',
                           'wasm_function': function['name'] if function else None,
                           'position_source_anchor': anchor,
                           'allocation_site': None if wrapper or not anchor or not anchor[1] else anchor[1:]})
    result = {'wasm_sha256': module['sha256'],
              'source_map_sha256': hashlib.sha256(source_map.read_bytes()).hexdigest(),
              'source_map_anchors': len(anchors), 'targets': summaries,
              'wasm_frame_attribution': mapped}
    (capture / 'source-memory-attribution.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def snapshot_summary(file):
    snapshot = json.loads(file.read_text())
    fields = snapshot['snapshot']['meta']['node_fields']
    stride = len(fields)
    types = snapshot['snapshot']['meta']['node_types'][0]
    rows = []
    for offset in range(0, len(snapshot['nodes']), stride):
        node = snapshot['nodes'][offset:offset + stride]
        name = snapshot['strings'][node[fields.index('name')]]
        if name in ('system / JSArrayBufferData', 'WebAssembly.Memory', 'SharedArrayBuffer'):
            rows.append({'type': types[node[fields.index('type')]], 'name': name,
                         'self_bytes': node[fields.index('self_size')]})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--wasm', type=Path, required=True)
    parser.add_argument('--map', dest='source_map', type=Path, required=True)
    parser.add_argument('--program', required=True)
    args = parser.parse_args()
    result = analyze(args.capture, args.wasm, args.source_map, args.program)
    print(json.dumps({'targets': [{'name': t['target'],
                                 'v8_bytes': t.get('v8', {}).get('sampled_bytes'),
                                 'native_bytes': t.get('native', {}).get('attributed_bytes'),
                                 'errors': t['errors']} for t in result['targets']],
                      'wasm_frame_attribution': result['wasm_frame_attribution']}, indent=2))


if __name__ == '__main__':
    main()
