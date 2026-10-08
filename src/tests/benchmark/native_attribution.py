#!/usr/bin/env python3
"""Join native samples, V8 code lifetimes, WASM modules and ARM/C++ source maps.

Only V8 source anchors and trapping instructions receive instruction attribution.
An annotation does not label every following native instruction until the next
annotation: V8 omits arithmetic positions. Unmapped samples remain explicit.
"""
import argparse, bisect, collections, hashlib, json, pathlib, re, struct, subprocess

KINDS = ['unknown', 'guest', 'state', 'memory_check', 'guest_memory', 'accounting', 'dispatch', 'helper']
BASE64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'

class Reader:
    def __init__(self, data): self.data, self.pos = data, 0
    def byte(self):
        value = self.data[self.pos]; self.pos += 1; return value
    def leb(self):
        value = 0
        for n in range(10):
            byte = self.byte(); value |= (byte & 127) << (7*n)
            if not byte & 128: return value
        raise ValueError('Invalid LEB')
    def text(self):
        n = self.leb(); value = self.data[self.pos:self.pos+n].decode(); self.pos += n; return value
    def limits(self):
        flags = self.leb(); self.leb()
        if flags & 1: self.leb()


def module_info(file):
    data = file.read_bytes(); r = Reader(data); assert data[:8] == b'\0asm\1\0\0\0'; r.pos = 8
    imports, names, functions, provenance = 0, {}, {}, {}
    while r.pos < len(data):
        kind, size = r.byte(), r.leb(); start = r.pos; end = start + size
        assert end <= len(data)
        s = Reader(data[start:end])
        if kind == 2:
            for _ in range(s.leb()):
                s.text(); s.text(); tag = s.byte()
                if tag == 0: s.leb(); imports += 1
                elif tag == 1: s.byte(); s.limits()
                elif tag == 2: s.limits()
                elif tag == 3: s.byte(); s.byte()
                elif tag == 4: s.leb(); s.leb()
                else: raise ValueError('Unknown import kind')
        elif kind == 10:
            for index in range(imports, imports+s.leb()):
                n = s.leb(); functions[index] = {'start': start+s.pos, 'size': n}; s.pos += n
            assert s.pos == size
        elif kind == 0:
            name = s.text()
            if name == 'name':
                while s.pos < size:
                    tag, n = s.byte(), s.leb(); stop = s.pos+n
                    if tag == 1:
                        for _ in range(s.leb()):
                            index = s.leb(); names[index] = s.text()
                    s.pos = stop
            elif name == 'eka2l1.sources':
                assert s.leb() == 1
                for _ in range(s.leb()):
                    index, count = s.leb(), s.leb()
                    marks = [[s.leb(),s.leb(),s.leb()] for _ in range(count)]
                    assert all(a[0] <= b[0] for a,b in zip(marks,marks[1:]))
                    provenance[index] = marks
                assert s.pos == size
        r.pos = end
    for index, function in functions.items():
        function.update(name=names.get(index,f'wasm-function[{index}]'), marks=provenance.get(index,[]))
        assert all(0 <= m[0] < function['size'] and m[2] < len(KINDS) for m in function['marks'])
    return {'file': str(file), 'sha256': hashlib.sha256(data).hexdigest(), 'functions': functions}


def vlq(n):
    n = ((-n)<<1)|1 if n<0 else n<<1; result = ''
    while True:
        digit = n & 31; n >>= 5; result += BASE64[digit | (32 if n else 0)]
        if not n: return result


def export_guest_map(module, output):
    records=[]
    for index, function in module['functions'].items():
        for offset,pc,kind in function['marks']:
            records.append((function['start']+offset, f'{function["name"]} guest_pc=0x{pc:08x} {KINDS[kind]}'))
    if not records: return
    records.sort(); lines=[]; segments=[]; old_column=old_line=0
    for column,text in records:
        line=len(lines);lines.append(text)
        segments.append(vlq(column-old_column)+'A'+vlq(line-old_line)+'A')
        old_column,old_line=column,line
    stem=module['sha256'];source=stem+'.arm.txt'
    (output/source).write_text('\n'.join(lines)+'\n')
    (output/(stem+'.wasm.map')).write_text(json.dumps({'version':3,'file':pathlib.Path(module['file']).name,
        'sources':[source],'sourcesContent':['\n'.join(lines)+'\n'],'names':[],'mappings':','.join(segments),
        'x_wasm_sha256':stem}))


def load_source_map(file):
    data=json.loads(file.read_text());entries=[];column=source=line=col=0
    assert ';' not in data['mappings'], 'Expected single-line WASM source map'
    for segment in data['mappings'].split(','):
        if not segment: continue
        values=[];value=shift=0
        for char in segment:
            digit=BASE64.index(char);value|=(digit&31)<<shift
            if digit&32:shift+=5
            else:
                values.append(-(value>>1) if value&1 else value>>1);value=shift=0
        column+=values[0]
        if len(values)>=4:
            source+=values[1];line+=values[2];col+=values[3]
            entries.append((column,data['sources'][source],line+1,col+1))
        else: entries.append((column,None,None,None))
    return [e[0] for e in entries], entries


def jit_records(file):
    b=file.read_bytes();assert len(b)>=40 and struct.unpack_from('<II',b)==(0x4a695444,1)
    assert struct.unpack_from('<I',b,12)[0]==62 and struct.unpack_from('<Q',b,32)[0]==0, 'Expected x64 monotonic jitdump'
    pos=struct.unpack_from('<I',b,8)[0]
    while pos+16<=len(b):
        kind,n,t=struct.unpack_from('<IIQ',b,pos)
        assert n>=16
        if pos+n>len(b): break
        if kind==0:
            pid,tid,vma,address,size,index=struct.unpack_from('<IIQQQQ',b,pos+16)
            end=b.index(0,pos+56,pos+n);code=b[end+1:pos+n];assert len(code)==size
            yield dict(pid=pid,tid=tid,timestamp=t,address=address,size=size,id=index,
                name=b[pos+56:end].decode(),sha256=hashlib.sha256(code).hexdigest())
        elif kind==1: raise ValueError('Code move requires lifetime adapter')
        pos+=n


def disassemble(file):
    text=subprocess.check_output(['objdump','-D','-b','binary','-m','i386:x86-64','-Mintel','--insn-width=16',str(file)],text=True)
    instructions={}
    for line in text.splitlines():
        m=re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(\S.*)',line)
        if m: instructions[int(m[1],16)]={'bytes':len(m[2].split()),'asm':m[3]}
    file.with_suffix('.asm').write_text(text)
    return instructions


def match_code(candidates, timestamp, ip):
    valid=[c for c in candidates if c['timestamp']<=timestamp and c['address']<=ip<c['address']+c['size']]
    return max(valid,key=lambda c:c['timestamp']) if valid else None


def source_anchors(code):
    anchors={p['native_offset']:p for p in code['positions']}
    position_offsets=[p['native_offset'] for p in code['positions']]
    assert position_offsets==sorted(position_offsets)
    # V8 may emit several trapping instructions for one source operation. Only
    # registered traps inherit the preceding position; arithmetic does not.
    for offset in code['traps']:
        index=bisect.bisect_right(position_offsets,offset)-1
        if index>=0: anchors[offset]=code['positions'][index]
    return anchors


def analyze(capture, cpp_map=None, cpp_wasm=None):
    samples=json.loads((capture/'native-samples.json').read_text())
    assert not samples['losses'] and not samples['errors']
    metadata=json.loads((capture/'native-metadata.json').read_text())
    assert metadata['adapter_version']==1, 'Unsupported native metadata adapter'
    modules={}
    for file in capture.glob('*-module-*.wasm'):
        mod=module_info(file);modules[mod['sha256']]=mod
    if not any(f['marks'] for m in modules.values() for f in m['functions'].values()):
        raise ValueError('No guest source metadata: build with EKA2L1_AOT_SOURCE_MAPS=ON')
    bundle_file=capture/'runtime-source-map.json'
    bundle=json.loads(bundle_file.read_text()) if bundle_file.exists() else None
    if cpp_map is None and bundle:
        cpp_map=capture/'runtime.wasm.map'
        cpp_wasm=pathlib.Path(modules[bundle['wasm_sha256']]['file'])
    if cpp_wasm:
        mod=module_info(cpp_wasm);cpp_hash=mod['sha256']
        if cpp_hash not in modules: raise ValueError('C++ module was not captured from this gameplay run')
        if bundle:
            assert cpp_hash == bundle['wasm_sha256']
            assert hashlib.sha256(cpp_map.read_bytes()).hexdigest() == bundle['map_sha256']
        if (capture/'report.json').exists():
            assert cpp_hash == json.loads((capture/'report.json').read_text())['wasm_sha256']
        modules[cpp_hash]=mod
    else:cpp_hash=None
    cpp=load_source_map(cpp_map) if cpp_map else None
    mapped=capture/'source-maps';mapped.mkdir(exist_ok=True)
    for mod in modules.values():export_guest_map(mod,mapped)
    candidates=collections.defaultdict(list)
    for digest,mod in modules.items():
        for index,f in mod['functions'].items():candidates[(f['name'],index)].append(digest)
    native_modules=collections.defaultdict(list)
    for c in metadata['found']:
        name=re.sub(r'-\d+-(turbofan|liftoff)$','',c['name'].removeprefix('JS:'))
        possible=set(candidates.get((name,c['function_index']),[]))
        if possible:native_modules[(c['pid'],c['native_module'])].append(possible)
    module_matches={key:set.intersection(*choices) for key,choices in native_modules.items()}
    code_data={}
    for c in metadata['found']:
        matches=module_matches.get((c['pid'],c['native_module']),set())
        digest=next(iter(matches)) if len(matches)==1 else None
        binary=capture/f'native-{c["pid"]}-{c["id"]}.bin'
        assert hashlib.sha256(binary.read_bytes()).hexdigest()==c['sha256']
        insns=disassemble(binary);anchors=source_anchors(c)
        locations={}
        for offset,position in anchors.items():
            if offset not in insns or not digest:continue
            function_index=c['function_index'];inlining_id=position['inlining_id']
            if inlining_id>=0:
                if inlining_id>=len(c['inlinees']):continue
                function_index=c['inlinees'][inlining_id]['function_index']
            function=modules[digest]['functions'].get(function_index)
            if not function or not 0<=position['wasm_offset']<function['size']:continue
            base={'module_sha256':digest,'wasm_function_index':function_index,'wasm_offset':position['wasm_offset'],
                'native_anchor':offset,'v8_inlining_id':inlining_id}
            if digest==cpp_hash and cpp:
                index=bisect.bisect_right(cpp[0],function['start']+position['wasm_offset'])-1
                if index>=0 and cpp[1][index][1]:
                    _,file,line,column=cpp[1][index]
                    if bundle and bundle.get('map_directory'):
                        file=str((pathlib.Path(bundle['map_directory'])/file).resolve())
                    locations[offset]={**base,'domain':'cpp','file':file,'line':line,'column':column}
            elif function['marks']:
                index=bisect.bisect_right([m[0] for m in function['marks']],position['wasm_offset'])-1
                if index>=0:
                    _,pc,kind=function['marks'][index];locations[offset]={**base,'domain':'arm','guest_pc':pc,'kind':KINDS[kind]}
        code_data[(c['pid'],c['id'])]={'code':c,'module':digest,'instructions':insns,'locations':locations,'hits':collections.Counter()}
    pages=collections.defaultdict(list)
    for file in capture.glob('jit-*.dump'):
        for c in jit_records(file):
            for page in range(c['address']//4096,(c['address']+c['size']-1)//4096+1):pages[(c['pid'],page)].append(c)
    totals=collections.Counter();threads=collections.Counter();function_counts=collections.Counter();unmatched=0
    for time,pid,tid,ip in samples['samples']:
        c=match_code(pages.get((pid,ip//4096),[]),time,ip)
        if not c:unmatched+=1;continue
        function_counts[c['name']]+=1
        if re.match(r'JS:(?:f_\d+|r_\d+_pc_\d+)',c['name']):threads[(pid,tid)]+=1
    if not threads:raise ValueError('No guest worker identified by generated-code samples')
    guest_thread=threads.most_common(1)[0][0];thread_samples=[]
    for time,pid,tid,ip in samples['samples']:
        if (pid,tid)!=guest_thread:continue
        totals['samples']+=1
        c=match_code(pages.get((pid,ip//4096),[]),time,ip)
        if not c:totals['outside_jit']+=1;continue
        thread_samples.append(c['name']);data=code_data.get((pid,c['id']))
        if not data:totals['jit_without_snapshot']+=1;continue
        offset=ip-c['address'];data['hits'][offset]+=1
        location=data['locations'].get(offset)
        if location:totals['mapped_'+location['domain']]+=1
        else:totals['no_instruction_position']+=1
    functions=[]
    for data in code_data.values():
        if not data['hits']:continue
        c=data['code'];hot=[]
        for offset,count in data['hits'].most_common():
            hot.append({'offset':offset,'samples':count,'instruction':data['instructions'].get(offset), 'location':data['locations'].get(offset)})
        annotated=[]
        for offset,instruction in data['instructions'].items():
            location=data['locations'].get(offset)
            source='' if not location else (f'{location["file"]}:{location["line"]}' if location['domain']=='cpp'
                else f'guest_pc=0x{location["guest_pc"]:08x} {location["kind"]}')
            annotated.append(f'{offset:08x}  {data["hits"][offset]:6}  {instruction["asm"]:<55} {source}')
        (capture/f'native-{c["pid"]}-{c["id"]}.annotated.asm').write_text('\n'.join(annotated)+'\n')
        functions.append({'name':c['name'],'pid':c['pid'],'code_id':c['id'],'address':c['address'], 'timestamp':c['timestamp'],
            'native_sha256':c['sha256'],'module_sha256':data['module'],'samples':sum(data['hits'].values()),
            'native_bytes':c['size'],'positions':len(c['positions']),'traps':len(c['traps']),
            'assembly':f'native-{c["pid"]}-{c["id"]}.asm','hot_instructions':hot})
    functions.sort(key=lambda c:-c['samples'])
    report={'scope':'Guest worker userspace task-clock samples; exact V8 anchors only. Source attribution is sparse, not interval interpolation.',
        'guest_thread':guest_thread,'totals':dict(totals),'functions':functions,
        'all_thread_samples':len(samples['samples']),'unmatched_all_threads':unmatched,
        'guest_worker_functions':collections.Counter(thread_samples).most_common(),
        'modules':[{k:m[k] for k in ('file','sha256')} for m in modules.values()],
        'cpp_source_map':str(cpp_map) if cpp_map else None,
        'cpp_source_map_sha256':hashlib.sha256(cpp_map.read_bytes()).hexdigest() if cpp_map else None}
    (capture/'native-attribution.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'guest_thread':guest_thread,'totals':dict(totals),'top':[(f['name'],f['samples']) for f in functions[:15]]},indent=2))
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=pathlib.Path)
    p.add_argument('--cpp-map',type=pathlib.Path);p.add_argument('--cpp-wasm',type=pathlib.Path);a=p.parse_args()
    if bool(a.cpp_map)!=bool(a.cpp_wasm):p.error('Supply both --cpp-map and --cpp-wasm')
    analyze(a.capture,a.cpp_map,a.cpp_wasm)

if __name__=='__main__':main()
