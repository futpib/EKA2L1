#!/usr/bin/env python3
"""Build an offline same/cross-instance indirect-call discriminator.

Input kernels come from eka_compiler_probe. Bodies, guards and state publication
are preserved. This does not implement emulator dispatch or code validation.
Generated guest code stays in the supplied scratch output directory.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess


def parse(text):
    stack = []
    root = None
    for token in re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text):
        if token == '(':
            node = []
            if stack:
                stack[-1].append(node)
            stack.append(node)
        elif token == ')':
            root = stack.pop()
        else:
            stack[-1].append(token)
    if stack:
        raise ValueError('Unclosed WAT expression')
    return root


def emit(node):
    return '(' + ' '.join(map(emit, node)) + ')' if isinstance(node, list) else node


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernels', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--bin', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir()
    manifest = {'scope': 'Offline ordinary-memory kernels; not whole-game dispatch', 'kernels': []}
    for pc, cycles in ((1879455500, 57), (1879129820, 7)):
        original = args.kernels / f'{pc}.wasm'
        wat = args.output / f'{pc}.source.wat'
        subprocess.run([str(args.bin / 'wasm-dis'), str(original), '-o', str(wat)], check=True)
        tree = parse(wat.read_text())
        functions = [n for n in tree if isinstance(n, list) and n[0] == 'func']
        if len(functions) != 1:
            raise ValueError('Expected one extracted kernel')
        function = functions[0]
        # Keep all types and imports exactly; replace only definitions/exports.
        module = ['module', *[n for n in tree[1:] if n[0] not in ('func', 'export')]]
        module.append(['import', '"env"', '"table"', ['table', '$dispatch', '32', 'funcref']])
        for index in range(32):
            clone = copy.deepcopy(function)
            clone[1] = f'$kernel{index}'
            module.extend([clone, ['export', f'"kernel{index}"', ['func', clone[1]]]])
        reset = '\n'.join(f'(i32.store offset={offset} (i32.const 1024) '
                          f'(i32.load offset={offset} (i32.const 4096)))'
                          for offset in range(0, 64, 4))
        # Each browser uses just one layout and width. The module bytes are
        # identical for same/cross; only the table's owning instances differ.
        for name, call in (
                ('indirect', '(call_indirect (param i32) (result i32) '
                             '(i32.const 1024) (i32.and (local.get $n) (local.get $mask)))'),
                ('direct', '(call $kernel0 (i32.const 1024))')):
            module.append(parse(f'''(func (export "{name}")
                (param $n i32) (param $mask i32) (result i32) (local $sum i32)
                (loop $again {reset}
                    (local.set $sum (i32.add (local.get $sum) {call}))
                    (local.set $n (i32.sub (local.get $n) (i32.const 1)))
                    (br_if $again (local.get $n))) (local.get $sum))'''))
        output_wat = args.output / f'{pc}.wat'
        output_wasm = args.output / f'{pc}.wasm'
        output_wat.write_text(emit(module) + '\n')
        subprocess.run([str(args.bin / 'wasm-as'), str(output_wat), '--enable-threads',
                        '-o', str(output_wasm)], check=True)
        manifest['kernels'].append(dict(pc=pc, cycles=cycles,
            source_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
            module_sha256=hashlib.sha256(output_wasm.read_bytes()).hexdigest()))
    (args.output / 'native-fixtures.json').write_bytes((args.kernels / 'native-fixtures.json').read_bytes())
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
