import os,json,struct,time,subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
root=Path(__file__).parent

def trace(p,out,binary,measure,label,seconds=8):
    maps=Path(f'/proc/{p.pid}/maps').read_text()
    base=next(int(line.split('-')[0],16) for line in maps.splitlines() if str(binary) in line and int(line.split()[2],16)==0)
    with open(binary,'rb') as f:
        elf=ELFFile(f);bias=base if elf.header['e_type']=='ET_DYN' else 0
        syms={s.name:int(s['st_value'])+bias for s in elf.get_section_by_name('.symtab').iter_symbols() if 'eka_trace' in s.name}
    fields={k:syms[f'_ZN9eka_trace{len(k)}{k}E'] for k in ['enabled','count','rows']}
    fd=os.open(f'/proc/{p.pid}/mem',os.O_RDWR)
    sampler=subprocess.Popen(['python3',str(root/'sample_switches.py'),str(out/(label+'-switches.json')),str(p.pid)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    assert sampler.stdout.readline().strip()=='READY'
    try:
        os.pwrite(fd,struct.pack('<I',0),fields['count']);os.pwrite(fd,struct.pack('<I',1),fields['enabled'])
        try:measurement=measure(label,seconds)
        finally:
            os.pwrite(fd,struct.pack('<I',0),fields['enabled']);sampler.stdin.write('\n');sampler.stdin.flush();sampler.wait(10)
        assert sampler.returncode==0,sampler.stderr.read()
        time.sleep(.1);n=struct.unpack('<I',os.pread(fd,4,fields['count']))[0];capacity=1048576
        data=os.pread(fd,min(n,capacity)*64,fields['rows']);names={}
        for row in struct.iter_unpack('<8Q',data):
            if row[2] in [9,11] and row[7] and row[7] not in names:names[row[7]]=os.pread(fd,256,row[7]).split(b'\0')[0].decode()
        (out/(label+'.trace')).write_bytes(data)
        report=dict(measurement=measurement,count=n,capacity=capacity,names=names,pid=p.pid,fields=fields)
        (out/(label+'-trace.json')).write_text(json.dumps(report,indent=2)+'\n');return report
    finally:os.close(fd)
