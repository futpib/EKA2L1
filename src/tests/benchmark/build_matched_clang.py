#!/usr/bin/env python3
"""Recompile just the shared reference with the SDK's native Clang, preserving library linkage."""
import pathlib,shlex,subprocess,sys,json
build,compiler,out=map(pathlib.Path,sys.argv[1:]);build=build.resolve();compiler=compiler.absolute();out=out.resolve();out.mkdir()
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','eka_matched_kernel'],text=True).splitlines()
compile=shlex.split(next(c for c in commands if ' -c ' in c and c.endswith('/matched_kernel.cpp')))
link=shlex.split(commands[-1]);link=link[2:-2] # remove ': &&' and '&& :'
old_object=compile[compile.index('-o')+1];new_object=str(out/'kernel.o');exe=str(out/'matched-clang')
compile[0]=str(compiler);compile[compile.index('-o')+1]=new_object
for flag in ['-MT','-MF']:
 if flag in compile:i=compile.index(flag);del compile[i:i+2]
compile=[s for s in compile if s not in ['-MD','-O2']]
link[0]=str(compiler);link=[new_object if s==old_object else s for s in link];link[link.index('-o')+1]=exe
link=[s for s in link if not s.startswith('-Wl,--dependency-file=')]
for command in [compile,link]:subprocess.run(command,cwd=build,check=True)
(out/'commands.json').write_text(json.dumps({'compile':compile,'link':link,'compiler':subprocess.check_output([str(compiler),'--version'],text=True)},indent=2))
