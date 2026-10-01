#!/usr/bin/env python3
"""Warm each isolated frontend to identical guest work, then time serially.
Diagnostic builds must be explicitly named; this script never publishes them.
"""
import argparse,pathlib,subprocess,os,time,json
p=argparse.ArgumentParser();p.add_argument('--assets',required=True,type=pathlib.Path);p.add_argument('--builds',required=True,type=pathlib.Path);p.add_argument('--output',required=True,type=pathlib.Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir();root=pathlib.Path(__file__).resolve().parents[3]
builds=json.loads(a.builds.read_text());plan=list(builds)+list(reversed(builds));jobs=[];logs=[]
try:
 for index,name in enumerate(plan):
  out=a.output/f'{index}-{name}';gate=a.output/f'{index}.release';log=(a.output/f'{index}-{name}.log').open('w');logs.append(log)
  env={**os.environ,'EKA2L1_GPU':'hardware','EKA2L1_PROFILE_DETAIL':'0','EKA2L1_BENCHMARK_AOT':'5','EKA2L1_PROFILE_START_US':'78000000','EKA2L1_WASM_BUILD_DIR':builds[name],'PROFILE_GATE':str(gate)}
  for key in ['EKA2L1_AOT_VERIFY','EKA2L1_V8_FLAGS','EKA2L1_V8_DUMP','EKA2L1_GUEST_PROFILE']:env.pop(key,None)
  proc=subprocess.Popen(['node','profile.ts',str(a.assets.resolve()),str(out),'2','0','96000000'],cwd=root/'src/tests/wasm',env=env,stdout=log,stderr=log);jobs.append((name,proc,gate,out))
 deadline=time.monotonic()+1800
 while not all(pathlib.Path(str(g)+'.ready').exists() for _,_,g,_ in jobs):
  if any(p.poll() is not None for _,p,_,_ in jobs):raise RuntimeError('Warmup exited; see logs')
  if time.monotonic()>deadline:raise RuntimeError('Warmup timeout')
  time.sleep(1)
 print('All fixtures paused. Serial timing begins.',flush=True)
 for name,proc,gate,out in jobs:
  gate.touch();status=proc.wait(timeout=600)
  if status:raise RuntimeError(f'{name} failed; see log')
  result=json.loads((out/'report.json').read_text());m=result['measurement'];assert m['last_instructions']-m['first_instructions']==3975200506 and m['presentations']==676
  print(name,m['wall_seconds'],flush=True)
finally:
 for _,proc,gate,_ in jobs:
  if proc.poll() is None:gate.touch(exist_ok=True);proc.terminate()
 for log in logs:log.close()
