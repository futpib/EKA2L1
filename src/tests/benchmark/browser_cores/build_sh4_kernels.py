"""Assemble equivalent SH4 algorithms; toolchain is private, not system-installed."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('toolchain',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
r=Path(__file__).resolve().parent;a.output.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,LD_LIBRARY_PATH=str(a.toolchain/'usr/lib/x86_64-linux-gnu'));arrays=[]
for k in range(4):
 source=a.output/f'sh4-{k}.s';obj=a.output/f'sh4-{k}.o';binary=a.output/f'sh4-{k}.bin'
 source.write_bytes(subprocess.check_output(['clang','-E','-P',f'-DWORKLOAD={k}',str(r/'sh4_kernels.S')]))
 subprocess.run([str(a.toolchain/'usr/bin/sh4-linux-gnu-as'),'-little',str(source),'-o',str(obj)],env=env,check=True)
 subprocess.run([str(a.toolchain/'usr/bin/sh4-linux-gnu-objcopy'),'-O','binary','-j','.text',str(obj),str(binary)],env=env,check=True)
 arrays.append(binary.read_bytes())
(r/'sh4_kernels.h').write_text('// Generated from sh4_kernels.S with GNU as 2.35.2, little endian.\n'+''.join(f'static const unsigned char sh4_kernel{k}[]={{'+','.join(map(str,b))+'};\n' for k,b in enumerate(arrays))+'static const unsigned char* sh4_kernels[]={sh4_kernel0,sh4_kernel1,sh4_kernel2,sh4_kernel3};\nstatic const unsigned sh4_kernel_sizes[]={'+','.join(str(len(b)) for b in arrays)+'};\n')
