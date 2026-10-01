#!/usr/bin/env python3
"""Link isolated research frontends; never replace the LAN-served artifacts."""
import argparse,pathlib,subprocess,shlex,shutil,json
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--base-ref',default='d50abf943c9cde68756fd413a055d140314f0cda');p.add_argument('--variant',choices=['connected','baseline','ceiling'],required=True);a=p.parse_args();a.build=a.build.resolve();a.output=a.output.resolve();a.output.mkdir();root=a.build.parent
if a.variant=='connected' and 'Experimental connected-call regions' not in (root/'src/emu/cpu/src/aot/arm_translator.cpp').read_text():
 raise RuntimeError('Apply connected_callee_experiment.patch and rebuild the CPU archive first')
archive=a.output/'libcpu.a';shutil.copy2(a.build/'src/emu/cpu/libcpu.a',archive)
def command(target):return subprocess.check_output(['ninja','-C',str(a.build),'-t','commands',target],text=True).splitlines()[-1]
commands=[]
for file in (['arm_translator','aot_runtime'] if a.variant!='connected' else [])+(['code_compare'] if a.variant=='ceiling' else []):
 source=a.output/(file+'.cpp')
 if file=='code_compare':body='#include <cpu/aot/code_cache.h>\nnamespace eka2l1::arm::aot {bool equal_code_bytes(const std::uint8_t*,const std::uint8_t*,std::size_t){return true;}}\n'
 else:body=subprocess.check_output(['git','show',a.base_ref+':src/emu/cpu/src/aot/'+file+'.cpp'],cwd=root,text=True)
 source.write_text(body);obj=f'src/emu/cpu/CMakeFiles/cpu.dir/src/aot/{file}.cpp.o';cmd=shlex.split(command(obj))
 for opt in ['-o','-MT','-MF']:cmd[cmd.index(opt)+1]=str(a.output/(file+'.cpp.o'+('.d' if opt=='-MF' else '')))
 cmd[-1]=str(source);subprocess.run(cmd,cwd=a.build,check=True);commands.append(cmd)
 emar=pathlib.Path(cmd[0]).with_name('emar');subprocess.run([str(emar),'r',str(archive),str(a.output/(file+'.cpp.o'))],check=True)
parts=shlex.split(command('src/emu/wasm/eka2l1.html'));i=next(i for i,v in enumerate(parts) if v.endswith('/em++'));parts=parts[i:];parts=parts[:parts.index('&&')] if '&&' in parts else parts
parts=[str(archive) if v=='src/emu/cpu/libcpu.a' else v for v in parts];parts[parts.index('-o')+1]=str(a.output/'eka2l1.html');subprocess.run(parts,cwd=a.build,check=True)
(a.output/'build.json').write_text(json.dumps({'variant':a.variant,'compile':commands,'link':parts},indent=2))
if a.variant=='ceiling':(a.output/'UNSAFE_DIAGNOSTIC_ONLY.txt').write_text('Skips guest code-byte validation. Timing ceiling only; never use for gameplay.\n')
