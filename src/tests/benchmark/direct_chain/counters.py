import sys,json,pathlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from scheduler_probe import HardwareCounters
counter=None
for line in sys.stdin:
 req=json.loads(line)
 if req['command']=='start':
  pid=req['pid'];stat=pathlib.Path(f'/proc/{pid}/stat').read_text()
  counter=HardwareCounters({'guest':{'pid':pid,'tid':pid,'start_ticks':int(stat.rsplit(')',1)[1].split()[19])}},True)
  counter.start();print(json.dumps({'ready':True}),flush=True)
 elif req['command']=='stop':
  print(json.dumps(counter.stop()),flush=True);counter.close();counter=None
if counter:counter.close()
