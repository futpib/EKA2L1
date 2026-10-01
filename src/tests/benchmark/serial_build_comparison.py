#!/usr/bin/env python3
"""Run each browser through warmup and timing before starting the next browser."""
import argparse,json,os,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('assets',type=pathlib.Path);p.add_argument('baseline',type=pathlib.Path);p.add_argument('candidate',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir()
root=pathlib.Path(__file__).resolve().parents[3]
rows=[]
for name,build in [('before-1',a.baseline),('after-1',a.candidate),('after-2',a.candidate),('before-2',a.baseline)]:
 env={**os.environ,'EKA2L1_WASM_BUILD_DIR':str(build.resolve()),'EKA2L1_BENCHMARK_AOT':'5','EKA2L1_GPU':'hardware','EKA2L1_PROFILE_DETAIL':'0','EKA2L1_PROFILE_START_US':'78000000'}
 for key in ['PROFILE_GATE','EKA2L1_AOT_VERIFY','EKA2L1_GUEST_PROFILE','EKA2L1_AOT_DIAGNOSTICS']:env.pop(key,None)
 output=a.output/name
 with (a.output/(name+'.log')).open('w') as log:
  subprocess.run(['node','profile.ts',str(a.assets.resolve()),str(output.resolve()),'2','0','96000000'],cwd=root/'src/tests/wasm',env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1800,check=True)
 report=json.loads((output/'report.json').read_text());rows.append({'name':name,'build':str(build.resolve()),'measurement':report['measurement']});print(json.dumps(rows[-1]),flush=True)
 (a.output/'measurements.json').write_text(json.dumps(rows,indent=2)+'\n')
