#!/usr/bin/env python3
"""Extract interpreter layout with the WASM CPU target's actual compiler flags."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    command = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands',
                                      'src/emu/cpu/CMakeFiles/cpu.dir/src/dyncom/armstate.cpp.o'], text=True)
    flags = shlex.split(command)
    flags = flags[:flags.index('-MD')]
    source = Path(__file__).resolve().parents[1] / 'wasm/allocation-profiler-layout.cpp'
    with tempfile.TemporaryDirectory(prefix='eka-allocation-layout-') as directory:
        assembly = Path(directory) / 'layout.s'
        subprocess.run(flags + ['-S', str(source), '-o', str(assembly)], check=True)
        text = assembly.read_text()
    values = {}
    for name in ('state_size', 'decoded_capacity', 'decoded_used_offset'):
        section = text.split(name + ':', 1)[1].split('.Lfunc_end', 1)[0]
        values[name] = int(re.search(r'i32.const\s+(\d+)', section)[1])
    args.output.write_text(json.dumps(values, indent=2) + '\n')
    print(json.dumps(values))


if __name__ == '__main__':
    main()
