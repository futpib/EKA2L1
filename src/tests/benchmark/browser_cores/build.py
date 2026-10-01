from pathlib import Path
import subprocess,struct
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('workspace',type=Path,help='scratch parent containing skyemu, cloudpilot, rpcemu and emsdk clones')
parser.add_argument('--output',type=Path)
args=parser.parse_args()
repo=Path(__file__).resolve().parent
out=args.output or args.workspace/'browser-cores'
out.mkdir(exist_ok=True)
arrays=[]
for i in range(4):
 obj=out/f'kernel{i}.o';binary=out/f'kernel{i}.bin'
 subprocess.run(['clang','--target=armv4t-none-eabi','-c',f'-DWORKLOAD={i}',str(repo/'kernels.S'),'-o',str(obj)],check=True)
 subprocess.run(['ld.lld','-Ttext=0x1000','--image-base=0','--oformat=binary',str(obj),'-o',str(binary)],check=True)
 b=binary.read_bytes(); arrays.append(b)
(repo/'kernels.h').write_text('// Generated from kernels.S by build.py; ARMv4T little endian.\n'+''.join(f'static const unsigned char kernel{i}[]={{'+','.join(map(str,b))+'};\n' for i,b in enumerate(arrays))+'static const unsigned char* kernels[]={kernel0,kernel1,kernel2,kernel3};\nstatic const unsigned kernel_sizes[]={'+','.join(str(len(b)) for b in arrays)+'};\n')
emcc=str(args.workspace/'emsdk/upstream/emscripten/emcc')
subprocess.run([emcc,str(repo/'skyemu.c'),'-I'+str(args.workspace/'skyemu/src'),'-O3','-sMODULARIZE=1','-sEXPORT_NAME=createCore','-sALLOW_MEMORY_GROWTH=1','-o',str(out/'skyemu.js')],check=True)

subprocess.run([emcc,str(repo/'rpcemu.c'),str(args.workspace/'rpcemu/src/arm.c'),str(args.workspace/'rpcemu/src/arm_common.c'),'-DTEST','-I'+str(args.workspace/'rpcemu/src'),'-O3','-sMODULARIZE=1','-sEXPORT_NAME=createCore','-sALLOW_MEMORY_GROWTH=1','-o',str(out/'rpcemu.js')],check=True)

base=args.workspace/'cloudpilot/src'
names=['CPU','MMU','MPU','icache','cp15mmu','cp15mpu','gdbstub','mem','RAM','ROM','memory_buffer','system_state','patch_dispatch','memcpy','peephole','pxa270_WMMX']
cloud_args=[str(args.workspace/'emsdk/upstream/emscripten/em++'),str(repo/'cloudpilot.cpp'),str(repo/'cloudpilot_unused.cpp')]+[str(base/'uarm/uarm'/f'{n}.cpp') for n in names]+[str(base/'uarm/cputil.c')]
cloud_args+=['-I'+str(base/'uarm/uarm'),'-I'+str(base/'uarm'),'-I'+str(base/'common'),'-g','-std=c++17','-O3','-sMODULARIZE=1','-sEXPORT_NAME=createCore','-sALLOW_MEMORY_GROWTH=1','-o',str(out/'cloudpilot.js')]
subprocess.run(cloud_args,check=True)

import shutil
shutil.copy2(out/'cloudpilot.wasm',out/'cloudpilot-indirect.wasm')
post=base/'uarm/tools/build-jump-table'
subprocess.run([str(post/'node_modules/.bin/tsx'),'main.ts',str(out/'cloudpilot-indirect.wasm'),str(out/'cloudpilot.wasm')],cwd=post,check=True)
subprocess.run([str(args.workspace/'emsdk/upstream/bin/wasm-opt'),str(out/'cloudpilot.wasm'),'--enable-bulk-memory','--enable-bulk-memory-opt','--enable-sign-ext','--enable-nontrapping-float-to-int','--inlining-optimizing','-fimfs','1000','-ifwl','-pii','1000','-aimfs','10','-tnh','-lmu','-O4','-o',str(out/'cloudpilot-optimized.wasm')],check=True)
shutil.copy2(out/'cloudpilot-optimized.wasm',out/'cloudpilot.wasm')
