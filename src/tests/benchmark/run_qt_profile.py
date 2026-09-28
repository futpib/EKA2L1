#!/usr/bin/env python3
"""Serial native interpreter/JIT throughput trials; requires an accelerated X display."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser()
p.add_argument('--assets', type=Path, required=True)
p.add_argument('--template', type=Path, required=True, help='Fresh device-only state from run_native.py')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--backends', nargs='+', choices=['dyncom','dynarmic'], default=['dyncom','dynarmic','dynarmic','dyncom'])
p.add_argument('--detail', action='store_true', help='Enable common phase/call timing instrumentation')
p.add_argument('--sample', action='store_true', help='Guest-thread Linux/x86-64 PC sampling, diagnostic runs only')
p.add_argument('--timeout', type=int, default=300)
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(exist_ok=False, parents=True)
binary = ROOT/'build/bin/eka2l1_qt'
replay = Path(__file__).with_name('snakes.input').resolve()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          'dirty_worktree':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),
          'binary_sha256':sha(binary), 'input_sha256':sha(replay),
          'assets':{name:sha(a.assets/name) for name in ['SYM.ROM','SYM.RPKG','Snakes.sis']},
          'backends':a.backends,'detail':a.detail,'sample':a.sample,
          'method':'Serial configured backends; virtual clock, 78-96 guest seconds, rendering on, no capture/audio retention, no host pacing. JIT exits need not match exact interpreter budgets.',
          'glxinfo':subprocess.check_output(['glxinfo','-B'],text=True), 'runs':[]}
for i,backend in enumerate(a.backends):
    directory=a.output/f'{i}-{backend}'
    shutil.copytree(a.template,directory/'state')
    env={**os.environ,'QT_QPA_PLATFORM':'xcb','TZ':'UTC','EKA2L1_BENCHMARK':'1',
         'EKA2L1_BENCHMARK_INPUT':str(replay),'EKA2L1_QT_PROFILE_CPU':backend,
         'EKA2L1_QT_PROFILE_OUTPUT':str(directory/'measurement.json'),
         'XDG_DATA_HOME':str(directory/'state/data'),'XDG_CONFIG_HOME':str(directory/'state/config'),
         '__GL_SYNC_TO_VBLANK':'0'}
    env.pop('LIBGL_ALWAYS_SOFTWARE',None)
    for key in ['EKA2L1_NATIVE_SAMPLE_OUTPUT','EKA2L1_QT_PROFILE_DETAIL','PERF_BUILDID_DIR','EKA2L1_NATIVE_STUB_MAP']: env.pop(key,None)
    if a.detail: env['EKA2L1_QT_PROFILE_DETAIL']='1'
    if a.sample:
        env['EKA2L1_NATIVE_SAMPLE_OUTPUT']=str(directory/'native-pcs.tsv')
        env['PERF_BUILDID_DIR']=str(directory)
        if os.environ.get('EKA2L1_NATIVE_STUB_SIDECAR') == '1':
            env['EKA2L1_NATIVE_STUB_MAP']=str(directory/'native-stubs.map')
    with (directory/'run.log').open('w') as log:
        subprocess.run([str(binary),'--install',str(a.assets.resolve()/'Snakes.sis'),'--run','Snakes'],
                       env=env,stdout=log,stderr=subprocess.STDOUT,timeout=a.timeout,check=True)
    measurement=json.loads((directory/'measurement.json').read_text())
    report['runs'].append({'backend':backend,'measurement':measurement})
    (a.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(backend,json.dumps(measurement),flush=True)
