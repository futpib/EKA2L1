from pathlib import Path
import subprocess
import sys
repo=Path(__file__).resolve().parent;out=Path(sys.argv[1])
assembly='.syntax unified\n.arm\n.global _start\n_start:\n ldr sp, =0x41000000\n bl main_guest\n b .\n'
s=(repo/'kernels.S').read_text();parts=[s.split('#if WORKLOAD == 0\n')[1].split('#elif')[0],s.split('#elif WORKLOAD == 1\n')[1].split('#else')[0],s.split('#else\n')[1].split('#endif')[0]]
for k,body in enumerate(parts):
 assembly+=f'.global kernel{k}\nkernel{k}:\n push {{r4-r11,lr}}\n mov r2,r1\n mov r1,r0\n ldr r0,=0x12345678\n ldr r3,=0x9abcdef0\n mov r4,#0\n mov r5,#0\n'+body+'\n ldr r12,=results\n stmia r12,{r0-r5}\n pop {r4-r11,pc}\n.ltorg\n'
(out/'qemu_guest.S').write_text(assembly)
subprocess.run(['clang','--target=arm-none-eabi','-march=armv7-a','-marm','-mfpu=none','-mfloat-abi=soft','-fno-vectorize','-fno-slp-vectorize','-O2','-ffreestanding','-fno-builtin','-nostdlib','-fuse-ld=lld','-Wl,-Ttext=0x40010000','-Wl,--entry=_start','-Wl,--image-base=0x40000000',str(out/'qemu_guest.S'),str(repo/'qemu_guest.c'),'-o',str(out/'qemu_guest.elf')],check=True)
