"""Check semihosted ARM results; preserve timing as a separately labelled method."""
import json,re,sys
from pathlib import Path
source=Path(sys.argv[1]);reference=Path(sys.argv[2]);out=Path(sys.argv[3])
d=json.loads(source.read_text());refs=json.loads(reference.read_text())['references']['1000000']
assert not d['errors'] and not d.get('failures'),d
start=None;end=None;parts=[];rows=[]
for item in d['logs']:
 for token in re.findall(r'CORE_START|CORE_END|\b[0-9a-f]{8}\b',item['text']):
  if token=='CORE_START':start=item['t'];parts=[];end=None
  elif token=='CORE_END':
   assert len(parts)==2,parts
   kind,rep=parts;end=item['t'];parts=[]
  else:
   parts.append(int(token,16))
   if end is not None and len(parts)==9:
    k,r,*vals=parts;expected=refs[k];assert(k,r)==(kind,rep)
    assert vals[:2]==expected[:2] and vals[3:6]==expected[3:6] and vals[6]==expected[-1],(k,r,vals,expected)
    rows.append({'kind':k,'rep':r,'ms':end-start,'registers_r0_r5':vals[:6],'memory_hash':vals[6]});end=None
assert len(rows)==24 and len({(r['kind'],r['rep']) for r in rows})==24,len(rows)
out.write_text(json.dumps({'source':str(source),'timing':'Host receipt of CORE_START to CORE_END; semihosting and delivery overhead included, different from direct exported-function timing','rows':rows},indent=2)+'\n')
print('Verified',len(rows),'results')
