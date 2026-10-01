#!/usr/bin/env python3
"""Reproduce the N80 benchmark assets from the exact archived firmware ZIP.

This is a converter for one hash-identified firmware, not a general FPSX importer.
The layouts follow loader/{fpsx,rom,rofs}.h and EKA2L1/rpkgmaker.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import zipfile
import zlib

ARCHIVE_SHA256 = '16440261e679e277680664585b5a11702ea623b9fd723c2de994e43f72660903'
CORE = 'n80_5.0719.0.2-prd_western_c00_cc.fpsx'
ROFX = 'n80_rofx_5.0719.0.2-prd.v05'
EXPECTED = {
    'SYM.ROM': '99fc5d84bc66a4dfa00f66a967dcd817e344c9ac7eb7ee6124d40b1e75278698',
    'SYM.RPKG': 'be82cd991db95e2e9128b48dc7b9618f653813ec864c63db1952821de2178be0',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def take(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            f'Out-of-bounds read: offset={offset:#x}, size={size:#x}')
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, take(data, offset, struct.calcsize(fmt)))


def filename(raw):
    result = raw.decode('utf-16le').lower()
    require(result and result not in ('.', '..') and
            not any(c in result for c in '/\\:\0'), 'Invalid path component')
    return result


def blocks(data):
    require(data[0] == 0xb2, 'Not an FPSX file')
    offset = 5 + unpack('>I', data, 1)[0]
    result = []
    while offset < len(data):
        content, _, kind, size = unpack('4B', data, offset)
        header = take(data, offset + 4, size)
        if kind == 0x17:
            length, address = unpack('>II', header, 6)
            asic = header[0]
            description = b''
        elif kind in (0x27, 0x28):
            length, address = unpack('>II', header, 37)
            asic = header[32]
            description = header[20:32].rstrip(b'\0')
        else:
            raise ValueError(f'Unexpected block type {kind:#x}')
        start = offset + 5 + size
        result.append((content, kind, asic, address, description,
                       take(data, start, length)))
        # Old N80 containers have no implicit 512-byte block padding.
        offset = start + length
    require(offset == len(data), 'Incomplete FPSX parse')
    return result


def code_image(parsed, start, end):
    selected = [b for b in parsed if b[:3] == (0x54, 0x17, 1)
                and start <= b[3] < end]
    require(selected and selected[0][3] == start, 'Missing image start')
    for a, b in zip(selected, selected[1:]):
        require(a[3] + len(a[5]) == b[3], 'Non-contiguous image blocks')
    return b''.join(b[5] for b in selected)


def extract_rom(rom, files, counts):
    base, _, root = unpack('<3I', rom, 140)
    require(base == 0xf8000000, 'Unexpected ROM base')
    require(unpack('<I', rom, 244)[0] == len(rom), 'Wrong ROM length')
    seen = set()

    def directory(address, path):
        offset = address - base
        require(offset not in seen, 'Repeated ROM directory')
        seen.add(offset)
        end = offset + 4 + unpack('<I', rom, offset)[0]
        pos = offset + 4
        while pos < end:
            size, address, attributes, length = unpack('<IIBB', rom, pos)
            target = path / filename(take(rom, pos + 10, length * 2))
            pos = (pos + 10 + length * 2 + 3) & ~3
            if attributes & 0x10:
                directory(address, target)
            else:
                files[target] = take(rom, address - base, size)
                counts['rom'] += 1
        require(pos == end, 'Wrong ROM directory length')

    require(unpack('<I', rom, root - base)[0] == 1, 'Expected one ROM root')
    directory(unpack('<I', rom, root - base + 8)[0], Path())


def extract_rofs(data, original_rofs, files, counts, kind):
    header = unpack('<4sBBH4IQBBHIII', data, 0)
    magic, header_size, _, version, tree = header[:5]
    require(magic in (b'ROFS', b'ROFx') and version == 0x200,
            'Unexpected ROFS format')
    require(header[12] <= len(data), 'Truncated ROFS image')
    delta = tree - header_size
    seen = set()

    def entry(offset):
        size = unpack('<H', data, offset)[0]
        name_offset, attributes, length, address, _, name_length = unpack(
            '<BBIIBB', data, offset + 18)
        require(size >= 30 and name_offset + 2 * name_length <= size,
                'Invalid ROFS entry')
        name = filename(take(data, offset + name_offset, name_length * 2))
        return size, name, length, address, attributes

    def directory(address, path):
        offset = address - delta
        require(offset not in seen, 'Repeated ROFS directory')
        seen.add(offset)
        size, _, first, file_block, file_length = unpack('<HBBII', data, offset)
        require(first == 12, 'Unexpected ROFS directory header')
        pos = offset + first
        while pos < offset + size:
            entry_size, name, _, address, _ = entry(pos)
            directory(address, path / name)
            pos += entry_size
        require(pos == (offset + size + 3) & ~3, 'Wrong ROFS directory length')
        pos = file_block - delta
        end = pos + file_length
        while pos < end:
            entry_size, name, size, address, _ = entry(pos)
            target = path / name
            if address == 0xffffffff:
                files.pop(target, None)
                counts[kind + '_hidden'] += 1
            elif address < delta:
                # The language overlay also references unchanged base ROFS files.
                files[target] = take(original_rofs, address, size)
                counts[kind + '_reference'] += 1
            else:
                files[target] = take(data, address - delta, size)
                counts[kind] += 1
            pos += entry_size
        require(pos == (end + 3) & ~3, 'Wrong ROFS file block length')

    directory(tree, Path())


def convert(archive, output):
    require(hashlib.sha256(archive.read_bytes()).hexdigest() == ARCHIVE_SHA256,
            'Archive SHA-256 mismatch; this converter only supports the documented ZIP')
    with zipfile.ZipFile(archive) as source:
        require(source.testzip() is None, 'ZIP CRC check failed')
        members = {Path(n).name.lower(): n for n in source.namelist()}
        core = blocks(source.read(members[CORE]))
        rofx = blocks(source.read(members[ROFX]))

    certs = [b for b in core if b[:4] == (0x5d, 0x27, 1, 0x420000)
             and b[4] == b'SOS*CORE']
    require(len(certs) == 2 and certs[0][5][0x3d0:] == certs[1][5][0x3d0:],
            'Unexpected core certificates')
    packed = certs[0][5] + code_image(core, 0x420400, 0x12a0000)
    inflater = zlib.decompressobj(-15)
    decoded = inflater.decompress(packed[0x3d0:], 64 * 1024 * 1024)
    require(inflater.eof and not inflater.unconsumed_tail and
            inflater.unused_data == b'\xff' * 445, 'Incomplete core decompression')
    require(decoded[:0x30] == bytes(0x30), 'Unexpected core prefix')
    rom = decoded[0x30:]
    rofs = code_image(core, 0x12a0000, 0x10000000)
    overlay = code_image(rofx, 0x3920000, 0x10000000)
    files = {}
    counts = Counter()
    extract_rom(rom, files, counts)
    extract_rofs(rofs, rofs, files, counts, 'rofs')
    extract_rofs(overlay, rofs, files, counts, 'rofx')
    require(len(files) == 6819, 'Unexpected merged file count')

    output.mkdir(parents=True, exist_ok=False)
    (output / 'SYM.ROM').write_bytes(rom)
    with (output / 'SYM.RPKG').open('wb') as stream:
        stream.write(struct.pack('<8I', 82, 80, 75, 50, 0, len(files), 32, 0x200005f9))
        for path, content in sorted(files.items()):
            encoded = ('Z:\\' + str(path).replace('/', '\\')).encode('utf-16le')
            # Read-only/archive attributes and the base ROFS build timestamp.
            stream.write(struct.pack('<3Q', 33, 63348350400000000, len(encoded) // 2))
            stream.write(encoded)
            stream.write(struct.pack('<Q', len(content)))
            stream.write(content)
    report = {'archive_sha256': ARCHIVE_SHA256, 'entries': dict(counts),
              'merged_files': len(files), 'assets': {}}
    for name, expected in EXPECTED.items():
        data = (output / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        require(digest == expected, f'Output hash mismatch: {name}')
        report['assets'][name] = {'sha256': digest, 'bytes': len(data)}
    (output / 'extraction.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path, help='New output directory')
    args = parser.parse_args()
    convert(args.archive, args.output)
