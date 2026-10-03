#!/usr/bin/env python3
"""Map sampled generated-region entries to a capture's exact, expanded ROM.

This is offline attribution: it adds no guest instrumentation. RAM versions
remain unresolved without process/load metadata; an address alone is ambiguous.
The ROM structures follow loader/rom.h, rom.cpp and romimage.h.
"""
import argparse
import collections
import copy
import hashlib
import json
from pathlib import Path
import re
import struct


class Rom:
    def __init__(self, data):
        self.data = data
        self.base = self.u32(0x8c)
        if self.base == 0x50000000 or self.u32(0xec) or len(data) != self.u32(0xf4):
            raise ValueError('Requires an expanded EKA2 ROM; compressed and EKA1 layouts are unsupported')
        self.images = []
        self.visited = set()
        root = self.offset(self.u32(0x94))
        for i in range(self.u32(root)):
            self.directory(self.u32(root + 8 + i * 8), 'z:')

    def offset(self, address):
        offset = address - self.base
        if not 0 <= offset < len(self.data):
            raise ValueError(f'ROM address outside image: {address:#x}')
        return offset

    def u32(self, offset):
        if offset < 0 or offset + 4 > len(self.data):
            raise ValueError(f'ROM offset outside image: {offset:#x}')
        return struct.unpack_from('<I', self.data, offset)[0]

    def directory(self, address, parent):
        offset = self.offset(address)
        if offset in self.visited:
            return
        self.visited.add(offset)
        # The size covers entries, excluding the directory's four-byte header.
        end = offset + 4 + self.u32(offset)
        if not offset + 4 <= end <= len(self.data):
            raise ValueError('Invalid ROM directory extent')
        position = offset + 4
        while position < end:
            if position + 10 > end:
                raise ValueError('Truncated ROM directory entry')
            size, address, attributes, length = struct.unpack_from('<IIBB', self.data, position)
            next_position = position + 10 + 2 * length
            if next_position > end:
                raise ValueError('Truncated ROM filename')
            name = self.data[position + 10:next_position].decode('utf-16-le')
            path = parent + '/' + name
            if attributes & 0x10:
                self.directory(address, path)
            else:
                self.image(address, size, path)
            position = (next_position + 3) & ~3

    def image(self, address, size, path):
        if size < 68:
            return
        offset = self.offset(address)
        if self.u32(offset) not in (0x10000079, 0x1000007a):
            return
        base, code_size = self.u32(offset + 20), self.u32(offset + 28)
        if not self.base <= base < base + code_size <= self.base + len(self.data):
            return
        exports = collections.defaultdict(list)
        count, table = self.u32(offset + 60), self.u32(offset + 64)
        if count and self.base <= table <= table + count * 4 <= self.base + len(self.data):
            for i in range(count):
                exports[self.u32(table - self.base + i * 4) & ~1].append(i + 1)
        self.images.append(dict(path=path, base=base, size=code_size, exports=exports))

    def resolve(self, pc, symbols):
        matches = [image for image in self.images if image['base'] <= pc < image['base'] + image['size']]
        if len(matches) != 1:
            return None
        image = matches[0]
        ordinals = image['exports'].get(pc, [])
        definitions = symbols.get(Path(image['path']).name.lower(), {})
        return dict(module=image['path'], offset=pc - image['base'], exact_export_ordinals=ordinals,
                    exact_export_names=[definitions[o] for o in ordinals if o in definitions])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('rom', type=Path)
    parser.add_argument('--symbols', action='append', default=[], metavar='DLL=DEF_FILE',
                        help='Optional matching Symbian ordinal definitions; only exact entries get names')
    args = parser.parse_args()
    report = json.loads((args.capture / 'report.json').read_text())
    data = args.rom.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != report['assets']['SYM.ROM']:
        raise ValueError('ROM hash differs from the profiled asset')
    rom = Rom(data)
    symbols, symbol_sources = {}, {}
    for spec in args.symbols:
        name, filename = spec.split('=', 1)
        source = Path(filename).read_bytes()
        definitions = {}
        for line in source.decode().splitlines():
            match = re.match(r'\s*(\S+)\s+@\s+(\d+)\s+NONAME(?:\s*;\s*(.*))?', line)
            if match:
                definitions[int(match[2])] = match[3] or match[1]
        symbols[name.lower()] = definitions
        symbol_sources[name] = dict(path=filename, sha256=hashlib.sha256(source).hexdigest())
    summary = json.loads((args.capture / 'chrome-profile.json').read_text())
    worker = summary['likely_guest_worker']
    if not worker:
        raise ValueError('No sampled generated-code worker')
    profile = next(p for p in summary['profiles'] if p['name'] == worker)
    rows, modules = [], collections.Counter()
    unresolved_us = 0
    for frame in profile['frames']:
        guest = frame['guest']
        if not guest:
            continue
        mapping = rom.resolve(int(guest['pc'], 16), symbols) if guest['version'] is None else None
        rows.append({**frame, 'rom': mapping})
        if mapping:
            modules[mapping['module']] += frame['self_us']
        else:
            unresolved_us += frame['self_us']
    raw = json.loads((args.capture / f'{worker}.cpuprofile').read_text())
    labelled = copy.deepcopy(raw)
    by_id = {frame['node_id']: frame for frame in rows}
    for node in labelled['nodes']:
        frame = by_id.get(node['id'])
        if not frame:
            continue
        mapping = frame['rom']
        if mapping:
            location = ','.join(mapping['exact_export_names']) or f"+{mapping['offset']:#x}"
            label = f"{mapping['module']}!{location}"
        else:
            label = 'guest (module unresolved)'
        node['callFrame']['functionName'] = f"{label} [{frame['guest']['pc']}; {frame['name']}]"
    (args.capture / 'guest-rom.cpuprofile').write_text(json.dumps(labelled))
    result = dict(rom_sha256=digest, rom_images=len(rom.images), symbol_sources=symbol_sources,
                  worker=worker, sampled_us=profile['sampled_us'],
                  generated_self_us=profile['generated_self_us'], unresolved_guest_us=unresolved_us,
                  modules=[dict(module=m, self_us=us, self_percent=us / profile['sampled_us'] * 100)
                           for m, us in modules.most_common()], frames=rows,
                  limit='Region-entry attribution, not per-ARM-instruction timing. DLL names come from ROM ranges; supplied DEF names require matching ABI. RAM needs process, mapping lifetime and compiled-version metadata.')
    (args.capture / 'guest-rom.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'frames'}, indent=2))


if __name__ == '__main__':
    main()
