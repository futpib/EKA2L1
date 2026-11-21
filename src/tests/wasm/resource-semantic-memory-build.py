#!/usr/bin/env python3
"""Build real memory-model semantic-view controls from a verified runtime build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def sha(file):
    return hashlib.sha256(Path(file).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current-build', type=Path, required=True)
    parser.add_argument('--emxx', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--optimizer-loader', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {'schema': 1, 'pass': False, 'scope': 'Production multiple/flexible MMU resolver and backing publishers, without guest execution or hardware-permission claims.', 'errors': []}
    started = time.monotonic()
    pins = {}
    def pin(file):
        file = Path(file).resolve()
        pins[str(file)] = sha(file)
    try:
        configured = args.current_build.resolve()
        receipt = configured / 'bin/cooperative-build.json'
        pin(receipt)
        runtime = json.loads(receipt.read_text())
        if not runtime.get('semantic_js'):
            raise ValueError('A semantic-capable production build is required')
        for relative, expected in runtime['source_sha256'].items():
            file = ROOT / relative
            if sha(file) != expected:
                raise ValueError('Compiled source changed: ' + relative)
            pin(file)
        libraries = [f'src/emu/{folder}/lib{library}.a' for folder, library in
                     [('mem', 'epocmem'), ('cpu', 'cpu'), ('config', 'config'), ('common', 'common')]]
        libraries += [f'src/external/{name}/lib{name}.a' for name in ['spdlog', 'fmt', 'yaml-cpp']]
        libraries += ['src/external/libxxHash.a']
        origins = {}
        for relative in libraries:
            source, target = configured / relative, out / 'configured-build' / relative
            pin(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            origins[str(target)] = {'source': str(source), 'sha256': sha(target)}
        generated = configured / 'src/emu/common/include'
        shutil.copytree(generated, out / 'configured-build/src/emu/common/include')
        includes = [ROOT / 'src/emu' / name / 'include' for name in ['cpu', 'common', 'mem', 'config']]
        includes += [ROOT / 'src/external' / name / 'include' for name in ['spdlog', 'fmt', 'yaml-cpp']]
        includes += [out / 'configured-build/src/emu/common/include']
        for file in [Path(__file__), HERE / 'resource-semantic-memory.cpp', HERE / 'resource-semantic-memory-proof.mjs']:
            pin(file)
        for directory in includes:
            for file in directory.rglob('*'):
                if file.is_file():
                    pin(file)
        emxx = Path(shutil.which(args.emxx) or args.emxx).resolve()
        pin(emxx)
        report['toolchain'] = {'emxx': str(emxx), 'sha256': sha(emxx), 'version': subprocess.check_output([str(emxx), '--version'], text=True, timeout=30)}
        env = dict(os.environ)
        if args.optimizer_loader:
            loader = args.optimizer_loader.resolve()
            locked_file = loader.parent / 'receipt.json'
            locked = json.loads(locked_file.read_text())
            pin(loader)
            pin(locked_file)
            for relative, expected in locked['files'].items():
                file = loader.parent / relative
                if sha(file) != expected:
                    raise ValueError('Optimizer dependency changed: ' + str(file))
                pin(file)
            env['NODE_OPTIONS'] = '--import=' + json.dumps(str(loader))
        flags = ['-std=gnu++20', '-O2', '-fexceptions']
        flags += ['-DEKA2L1_' + name + '=1' for name in ['COOPERATIVE_JS', 'COUNTED_JS', 'COUNTED_LOOP_JS', 'COUNTED_REGION_JS', 'WORD_INCREMENT_JS', 'PACKED_PIXEL_JS', 'COUNTED_DIRECT_JS']]
        flags += ['-I' + str(directory) for directory in includes]
        flags += ['-Wl,--start-group', *[str(out / 'configured-build' / name) for name in libraries], '-Wl,--end-group', '--no-entry', '-sMALLOC=none', '-sWASM=0', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=node', '-sINITIAL_MEMORY=134217728', '-sALLOW_MEMORY_GROWTH=0', '-sASSERTIONS=1', '-sDISABLE_EXCEPTION_CATCHING=0', '-sEXPORTED_FUNCTIONS=_malloc,_free', '-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString']
        command = [str(emxx), str(HERE / 'resource-semantic-memory.cpp'), *flags, '-o', str(out / 'memory.mjs')]
        report.update(command=command, configured_input_origins=origins, inputs=pins)
        result = subprocess.run(command, env=env, capture_output=True, timeout=600)
        (out / 'build.log').write_bytes(result.stdout + result.stderr)
        report['exit_code'] = result.returncode
        if result.returncode:
            raise RuntimeError('Memory controls failed to build; see build.log')
        module = out / 'memory.mjs'
        raw = module.read_text()
        (out / 'memory-raw.mjs').write_text(raw)
        lookup = 'function findWasmBinary(){if(Module["locateFile"]){return locateFile("memory.wasm")}return new URL("memory.wasm",import.meta.url).href}'
        if raw.count(lookup) != 1:
            raise ValueError('Unknown ordinary JavaScript module glue')
        final = raw.replace(lookup, 'function findWasmBinary(){return ""}').replace('WebAssembly', 'GeneratedJsRuntime')
        if re.search(r'WebAssembly|SharedArrayBuffer|new\s+(?:Shared)?Worker\s*\(', final):
            raise ValueError('Forbidden runtime authority in memory controls')
        module.write_text(final)
        for file, expected in pins.items():
            if sha(file) != expected:
                raise ValueError('Consumed source changed: ' + file)
        for file, row in origins.items():
            if sha(file) != row['sha256']:
                raise ValueError('Configured archive snapshot changed: ' + file)
        report['outputs'] = {file.name: sha(file) for file in out.iterdir() if file.is_file()}
        report['pass'] = True
    except Exception as error:
        report['errors'].append(str(error))
    finally:
        report['elapsed_seconds'] = time.monotonic() - started
        (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'pass': report['pass'], 'seconds': report['elapsed_seconds'], 'errors': report['errors']}))
    return 0 if report['pass'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
