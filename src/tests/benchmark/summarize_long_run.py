#!/usr/bin/env python3
"""Summarize fixed guest-time windows from profile.ts long-monitor samples."""
import json,sys
from pathlib import Path
if len(sys.argv) < 2: raise SystemExit('Usage: summarize_long_run.py OUTPUT [OUTPUT ...]')
for dirname in sys.argv[1:]:
 p=Path(dirname); data=json.load(open(p/'timeline.json'))
 print(p.name, 'samples', len(data))
 def at(t):
  target=t*1e6
  if target<data[0]['guest_us'] or target>data[-1]['guest_us']:return None
  for a,b in zip(data,data[1:]):
   if a['guest_us']<=target<=b['guest_us']:
    q=(target-a['guest_us'])/(b['guest_us']-a['guest_us'])
    return {k:a[k]+q*(b[k]-a[k]) for k in ['guest_us','instructions','host_ms','allocated_bytes','free_bytes','pss_kib']}
 windows = [(22,60)] + [(n,n+60) for n in range(60,int(data[-1]['guest_us']/1e6),60)]
 for start,end in windows:
  a,b=at(start),at(end)
  if not a or not b:continue
  host=(b['host_ms']-a['host_ms'])/1000; inst=(b['instructions']-a['instructions'])/1e6
  print(f'{start}-{end}: {host:.3f}s host, {(end-start)/host:.3f}x, {inst/(end-start):.1f} Mguestinst/guestsec, {inst/host:.1f} MIPS, allocated {a["allocated_bytes"]/2**20:.2f}->{b["allocated_bytes"]/2**20:.2f} MiB, PSS {a["pss_kib"]/1024:.1f}->{b["pss_kib"]/1024:.1f} MiB')
 for d in [data[0],data[-1]]: print('endpoint', {k:v for k,v in d.items() if k not in ('heaps', 'worker_pool')})
 print('probe total ms',sum(d['probe_ms'] for d in data))
