#!/usr/bin/env python3
"""Run the real browser replay with request lifecycle instrumentation."""
import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
p.add_argument('assets', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--enabled', type=int, choices=[0, 1], default=1)
p.add_argument('--start-us', type=int, default=78000000)
p.add_argument('--end-us', type=int, default=96000000)
p.add_argument('--sampling', type=int, choices=[0, 1], default=1)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
wasm_tests = root / 'src/tests/wasm'
assets = a.assets.resolve()
with (assets / 'SYM.ROM').open('rb') as f:
    b = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    u = lambda off: struct.unpack_from('<I', b, off)[0]
    base = u(0x8c)
    rt = u(0x94) - base
    images = {}
    def walk(off):
        end = off + u(off)
        pos = off + 4
        while pos < end:
            size, address, attr, length = struct.unpack_from('<IIBB', b, pos)
            name = b[pos+10:pos+10+2*length].decode('utf-16le').lower()
            pos = (pos+10+2*length+3) & ~3
            if attr & 16: walk(address-base)
            else: images[name] = address
    for index in range(u(rt)):
        walk(u(rt+8+index*8)-base)
    ez = images['ezlib.dll']-base
    assert u(ez+60) >= 32
    entry = u(u(ez+64)-base + 31*4)

runner = a.output.resolve().with_suffix('.ts')
source = (wasm_tests / 'profile.ts').read_text()
source = source.replace("from './server.ts'", 'from ' + json.dumps((wasm_tests / 'server.ts').as_uri()))
source = source.replace("from 'puppeteer'", 'from ' + json.dumps((wasm_tests / 'node_modules/puppeteer/lib/esm/puppeteer/puppeteer.js').as_uri()))
anchor = "    call('eka2l1_init', ['string'], ['/data']);"
assert source.count(anchor) == 1
source = source.replace(anchor, f"    call('eka2l1_overlap_configure', ['number', 'number'], [{a.enabled}, {entry}]);\n" + anchor)
anchor = '  console.log(JSON.stringify(measured));'
assert source.count(anchor) == 1
source = source.replace(anchor, anchor + '''
  const overlap = await page.evaluate(() => JSON.parse((window as any).Module.ccall('eka2l1_overlap_report', 'string', [], [])));
  fs.writeFileSync(path.join(output, 'overlap.json'), JSON.stringify(overlap));
''')
runner.write_text(source)
env = dict(os.environ, EKA2L1_WASM_BUILD_DIR=str(a.build.resolve() / 'src/emu/wasm'),
           EKA2L1_GPU='hardware', EKA2L1_SHARED_AUDIO='1', EKA2L1_BENCHMARK_AOT='5',
           EKA2L1_AOT_IR_MODE='7', EKA2L1_PROFILE_DETAIL='1', EKA2L1_PROFILE_START_US=str(a.start_us),
           EKA2L1_PROFILE_INTERVAL_US='5000', EKA2L1_PROFILE_INPUT=str(root / 'src/tests/benchmark/snakes.input'))
for key in ['PROFILE_GATE', 'EKA2L1_GUEST_PROFILE', 'EKA2L1_EXIT_CENSUS', 'EKA2L1_AOT_VERIFY',
            'EKA2L1_V8_FLAGS', 'EKA2L1_LONG_MONITOR', 'EKA2L1_CAPTURE_MODULES']:
    env.pop(key, None)
with a.output.resolve().with_suffix('.log').open('w') as log:
    result = subprocess.run(['node', str(runner), str(assets), str(a.output.resolve()), '2', str(a.sampling), str(a.end_us)],
                            cwd=wasm_tests, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
result.check_returncode()
provenance = {'enabled': a.enabled, 'decompressor_pc': entry,
              'runner_sha256': hashlib.sha256(runner.read_bytes()).hexdigest(),
              'build_manifest': str(a.build.resolve() / 'manifest.json')}
(a.output / 'overlap-provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
print(a.output.resolve())
