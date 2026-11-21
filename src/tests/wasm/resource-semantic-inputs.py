#!/usr/bin/env python3
"""Extract the bounded original resource proof input from its pinned SIS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SIS_SHA = 'c3db14b9e3960b8041995a4bb8212bb54f7d18ac94d1c895dc6d9c79d3b25766'
INPUT_SHA = '7ec89ebcf595383ee3c0cabb05a3b31db711be840e4962faccfed8f8925ced9b'
def sha(file):
    return hashlib.sha256(Path(file).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sis', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {'schema': 1, 'pass': False, 'scope': 'Original pinned SIS loader extraction and original XOR resource read only; no decompression, state snapshot or CPU execution.', 'inputs': {}, 'errors': []}
    def pin(file):
        file = Path(file).resolve()
        report['inputs'][str(file)] = sha(file)
        return file
    try:
        sis = pin(args.sis)
        if sha(sis) != SIS_SHA:
            raise ValueError('Unexpected original SIS identity')
        generated = out / 'generated/common'
        generated.mkdir(parents=True)
        for name in ['configure.h', 'diagnostics_config.h']:
            template = pin(ROOT / 'src/emu/common/include/common' / (name + '.in'))
            text = '\n'.join('/* ' + line + ' */' if line.startswith('#cmakedefine') else line for line in template.read_text().splitlines()) + '\n'
            (generated / name).write_text(text)
        includes = [ROOT / 'src/emu' / name / 'include' for name in ['common', 'loader', 'cpu', 'mem', 'utils']]
        includes += [ROOT / 'src/external' / name / 'include' for name in ['spdlog', 'fmt']]
        for directory in includes:
            for file in directory.rglob('*'):
                if file.is_file() and file.suffix in {'.h', '.hpp', '.inc', '.in'}:
                    pin(file)
        sources = [pin(HERE / 'resource-semantic-extract.cpp'), pin(ROOT / 'src/emu/loader/src/sis_fields.cpp')]
        pin(__file__)
        compiler = shlex.split(args.cxx)
        if not compiler:
            raise ValueError('Missing host compiler')
        pin(shutil.which(compiler[0]) or compiler[0])
        command = [*compiler, '-std=c++17', '-O2', '-DNDEBUG', '-DDISABLE_LOGGING', '-ffunction-sections', '-fdata-sections', '-I' + str(generated.parent), *['-I' + str(p) for p in includes], *map(str, sources), '-Wl,--gc-sections', '-o', str(out / 'extract-sis'), '-lz']
        report['command'] = command
        for name, argv in [('build', command), ('extract', [str(out / 'extract-sis'), str(sis), str(out / 'installed')])]:
            result = subprocess.run(argv, capture_output=True, timeout=120)
            (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(name + ' failed; see retained log')
        pak = out / 'installed/private/a020d913/data.pak'
        raw = pak.read_bytes()
        if len(raw) < 20 or sha(pak) != 'a9459dfb37e2655a24325af8bb5126b9dc920786ed06360daabc5f76ef22e3e3':
            raise ValueError('Original installed pack identity differs')
        count = struct.unpack_from('<I', raw, 16)[0]
        if count != 139 or 20 + count * 16 > len(raw):
            raise ValueError('Original resource directory extent')
        directory = bytes(value ^ 0xaa for value in raw[20:20 + count * 16])
        rows = [struct.unpack_from('<IIII', directory, index * 16) for index in range(count)]
        selected = [row for row in rows if row[0] == 0x4ec6c278]
        if len(selected) != 1:
            raise ValueError('Missing or ambiguous original loading resource')
        key, offset, decoded, packed = selected[0]
        encoded, mode = packed & 0xffffff, packed >> 24
        if mode != 2 or encoded != 692 or decoded != 1796 or offset < 20 + count * 16 or offset > len(raw) - encoded:
            raise ValueError('Original resource bounds/mode differ')
        target = out / 'loading.input'
        target.write_bytes(bytes(value ^ 0xaa for value in raw[offset:offset + encoded]))
        if sha(target) != INPUT_SHA or struct.unpack_from('<I', target.read_bytes())[0] != decoded:
            raise ValueError('Original compressed resource identity differs')
        for file, expected in report['inputs'].items():
            if sha(file) != expected:
                raise ValueError('Consumed input changed: ' + file)
        report['resource'] = {'file': str(target), 'sha256': sha(target), 'bytes': encoded, 'decoded_bytes': decoded, 'key': key, 'original_slots': 1013982}
        report['outputs'] = {str(file.relative_to(out)): sha(file) for file in out.rglob('*') if file.is_file()}
        report['pass'] = True
    except Exception as error:
        report['errors'].append(str(error))
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'pass': report['pass'], 'resource': report.get('resource'), 'errors': report['errors']}))
    return 0 if report['pass'] else 1
if __name__ == '__main__':
    raise SystemExit(main())
