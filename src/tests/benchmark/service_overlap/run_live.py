#!/usr/bin/env python3
"""Observe the ordinary interactive path, including its existing graphics overlap."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
p.add_argument('assets', type=Path)
p.add_argument('reference', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--seconds', type=int, default=35)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
tests = root / 'src/tests/wasm'
reference = json.loads((a.reference / 'report.json').read_text())
for name, digest in reference['assets'].items():
    assert hashlib.sha256((a.assets / name).read_bytes()).hexdigest() == digest
entry = json.loads((a.reference / 'overlap.json').read_text())['decompressor_pc']
s = (tests / 'live.ts').read_text()
for old, new in [("from './server.ts'", 'from '+json.dumps((tests/'server.ts').as_uri())),
                 ("from 'puppeteer'", 'from '+json.dumps((tests/'node_modules/puppeteer/lib/esm/puppeteer/puppeteer.js').as_uri())),
                 ("from 'pngjs'", 'from '+json.dumps((tests/'node_modules/pngjs/lib/png.js').as_uri()))]:
    assert old in s
    s = s.replace(old, new)
# Keep Chromium's process-level mute; the app still runs the real AudioWorklet.
s = s.replace("ignoreDefaultArgs: audioEnabled ? ['--mute-audio'] : []", 'ignoreDefaultArgs: []')
anchor = '  if (!autoStart) {'
assert s.count(anchor) == 1
s = s.replace(anchor, f'''  await page.evaluate(() => (window as any).Module.ccall('eka2l1_overlap_configure','number',['number','number'],[1,{entry}]));
''' + anchor)
anchor = '  const finalResources = await resources();'
assert s.count(anchor) == 1
s = s.replace(anchor, anchor + '''
  const overlap=await page.evaluate(() => JSON.parse((window as any).Module.ccall('eka2l1_overlap_report','string',[],[])));
  fs.writeFileSync(path.join(output,'overlap.json'),JSON.stringify(overlap));
''')
runner = a.output.resolve().with_suffix('.ts')
runner.write_text(s)
env = dict(os.environ, EKA2L1_WASM_BUILD_DIR=str(a.build.resolve()/'src/emu/wasm'),
           EKA2L1_LIVE_AUDIO='1', EKA2L1_AOT_IR_MODE='7', EKA2L1_LIVE_AUTOSTART='0')
for key in ['EKA2L1_LIVE_PROFILE_START_US', 'EKA2L1_LIVE_PROFILE_END_US']:
    env.pop(key, None)
with a.output.resolve().with_suffix('.log').open('w') as log:
    result = subprocess.run(['node',str(runner),str(a.assets.resolve()),str(a.output.resolve()),str(a.seconds)],
                            cwd=tests,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600)
result.check_returncode()
print(a.output.resolve())
