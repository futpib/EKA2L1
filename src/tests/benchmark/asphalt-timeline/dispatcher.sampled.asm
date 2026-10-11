00000000      0 push   rbp
00000001      0 mov    rbp,rsp
00000004      0 push   0x30
00000006      0 push   rsi
00000007      0 sub    rsp,0x40
0000000b      0 mov    QWORD PTR [rbp-0x30],rax
0000000f      0 mov    QWORD PTR [rbp-0x28],rdx
00000013      0 cmp    rsp,QWORD PTR [r13-0x60]
00000017      0 jbe    0xf54
0000001d      0 mov    rbx,QWORD PTR [rsi+0x17]
00000021      0 mov    r8d,DWORD PTR [rbx+0x2546d4]
00000028      0 mov    r9d,eax
0000002b      0 mov    r11d,DWORD PTR [rbx+r9*1+0x7d083a4]
00000033      0 mov    QWORD PTR [rbp-0x18],rbx
00000037      0 mov    QWORD PTR [rbp-0x20],r9
0000003b      0 cmp    DWORD PTR [rbx+0x2546d8],0x2
00000042      0 je     0x6f2
00000048      0 lea    r12d,[r11+0x1d8]
0000004f      0 mov    r15d,DWORD PTR [rbx+r11*1+0x21d8]
00000057      0 xor    ecx,ecx
00000059      0 cmp    DWORD PTR [rbx+r11*1+0x21d8],0xc
00000062      0 cmove  ecx,r12d
00000066      0 mov    DWORD PTR [rbx+r9*1+0x350],ecx
0000006e      0 mov    QWORD PTR [rbp-0x40],r8
00000072      0 xor    r15d,r15d
00000075      0 call   0xffffffffff38f160
0000007a      0 cmp    DWORD PTR [rbp-0x40],0x3
0000007e      0 je     0x382
00000084      0 cmp    DWORD PTR [rbp-0x28],0x0
00000088      0 jne    0x95
0000008e      0 xor    eax,eax
00000090      0 mov    rsp,rbp
00000093      0 pop    rbp
00000094      0 ret
00000095      0 mov    QWORD PTR [rbp-0x38],rax
00000099      0 mov    edx,DWORD PTR [rbp-0x28]
0000009c      0 mov    r8,QWORD PTR [rbp-0x20]
000000a0      0 mov    rbx,QWORD PTR [rbp-0x18]
000000a4      0 jmp    0xc3
000000a9      0 nop    WORD PTR [rax+rax*1+0x0]
000000b2      0 nop    WORD PTR [rax+rax*1+0x0]
000000bb      0 nop    DWORD PTR [rax+rax*1+0x0]
000000c0      0 mov    eax,DWORD PTR [rbp-0x38]
000000c3      0 mov    rsi,QWORD PTR [rbp-0x10]
000000c7      0 cmp    BYTE PTR [r13-0x4f],0x0
000000cc      0 jne    0xf72
000000d2      0 mov    DWORD PTR [rbx+r8*1+0x35c],0x0
000000de      0 mov    r9d,edx
000000e1      0 mov    r11d,DWORD PTR [rsi+0x7]
000000e5      0 or     r11,QWORD PTR [r13+0x288]
000000ec      0 cmp    DWORD PTR [r11+0x7],edx
000000f0      0 jbe    0x107e
000000f6      0 imul   r9,r9,0xc
000000fa      0 mov    r12d,DWORD PTR [r11+r9*1+0x1f]
000000ff      0 mov    r15d,0x21
00000105      0 cmp    r15d,r12d
00000108      0 jne    0xf92
0000010e      0 mov    r12d,DWORD PTR [r11+r9*1+0x1b]
00000113      0 mov    r9d,DWORD PTR [r11+r9*1+0x23]
00000118      0 or     r9,QWORD PTR [r13+0x288]
0000011f      0 mov    rsi,r9
00000122      0 mov    eax,DWORD PTR [rbp-0x30]
00000125      0 movabs r10,0x7f56c99f0000
0000012f      0 shl    r12d,0x9
00000133      0 shr    r12d,0x5
00000137      0 cmp    QWORD PTR [r10+r12*1+0x8],0x8000001
00000140      0 je     0x143
00000142      0 int3
00000143      0 call   QWORD PTR [r10+r12*1]
00000147      0 test   eax,eax
00000149      0 je     0x35d
0000014f      0 mov    rbx,QWORD PTR [rbp-0x18]
00000153      0 mov    r8,QWORD PTR [rbp-0x20]
00000157      0 mov    r9,QWORD PTR [rbx+r8*1+0x348]
0000015f      0 cmp    QWORD PTR [rbx+r8*1+0x348],0x0
00000168      0 je     0x364
0000016e      0 mov    r9d,DWORD PTR [rbx+r8*1+0x368]
00000176      0 cmp    DWORD PTR [rbx+r8*1+0x368],0x0
0000017f      0 jne    0x19d
00000185      0 movzx  r9d,BYTE PTR [rbx+r8*1+0x310]
0000018e      0 test   BYTE PTR [rbx+r8*1+0x310],0x80
00000197      0 je     0x364
0000019d      0 lea    r9,[rbx+0x259840]
000001a4      0 and    r9d,0x3
000001a8      0 mov    r9d,r9d
000001ab      0 test   r9d,r9d
000001ae      0 jne    0x1079
000001b4      0 mov    r9d,DWORD PTR [rbx+0x259840]
000001bb      0 test   r9d,r9d
000001be      0 jne    0x364
000001c4      0 mov    r9d,DWORD PTR [rbx+r8*1+0x3c]
000001c9      0 mov    r9d,DWORD PTR [rbx+r8*1+0x33c]
000001d1      0 mov    r11d,0xfffffffc
000001d7      0 mov    r12d,0xfffffffe
000001dd      0 test   r9d,r9d
000001e0      0 cmovne r11d,r12d
000001e4      0 and    r11d,DWORD PTR [rbx+r8*1+0x3c]
000001e9      0 mov    DWORD PTR [rbx+r8*1+0x3c],r11d
000001ee      0 or     r9d,r11d
000001f1      0 cmp    BYTE PTR [rbx+0x262228],0x1
000001f8      0 jne    0x21e
000001fe      0 mov    r12d,DWORD PTR [rbx+0x2662c8]
00000205      0 cmp    r11d,r12d
00000208      0 jb     0x301
0000020e      0 sub    r11d,r12d
00000211      0 cmp    DWORD PTR [rbx+0x2662cc],r11d
00000218      0 jbe    0x301
0000021e      0 mov    QWORD PTR [rbx+r8*1+0x354],0x0
0000022a      0 mov    r11d,DWORD PTR [rbp-0x38]
0000022e      0 mov    r12d,DWORD PTR [rbx+r11*1+0x8020]
00000236      0 mov    r12d,r9d
00000239      0 sub    r12d,DWORD PTR [rbx+r11*1+0x8020]
00000241      0 mov    r15d,DWORD PTR [rbx+r11*1+0x8024]
00000249      0 cmp    DWORD PTR [rbx+r11*1+0x8024],r12d
00000251      0 ja     0x2bb
00000257      0 mov    r11d,r9d
0000025a      0 shl    r11d,0x7
0000025e      0 mov    r12d,r9d
00000261      0 shr    r12d,1
00000264      0 xor    r12d,r11d
00000267      0 and    r12d,0xfff
0000026e      0 mov    r11d,DWORD PTR [rbp-0x38]
00000272      0 lea    ecx,[r11+r12*8+0x14]
00000277      0 mov    edx,DWORD PTR [rbx+rcx*1+0x4]
0000027b      0 test   edx,edx
0000027d      0 je     0x291
00000283      0 mov    r12d,DWORD PTR [rbx+rcx*1]
00000287      0 cmp    DWORD PTR [rbx+rcx*1],r9d
0000028b      0 je     0xc0
00000291      0 mov    QWORD PTR [rbp-0x48],rax
00000295      0 mov    rsi,QWORD PTR [rbp-0x10]
00000299      0 mov    eax,r11d
0000029c      0 mov    edx,r9d
0000029f      0 call   0xffffffffff38f158
000002a4      0 test   eax,eax
000002a6      0 je     0x37a
000002ac      0 mov    edx,eax
000002ae      0 mov    rbx,QWORD PTR [rbp-0x18]
000002b2      0 mov    r8,QWORD PTR [rbp-0x20]
000002b6      0 jmp    0xc0
000002bb      0 mov    r9d,DWORD PTR [rbx+r11*1+0x8014]
000002c3      0 mov    r11d,r12d
000002c6      0 shr    r11d,0xa
000002ca      0 and    r11d,0x3ffffc
000002d1      0 add    r9d,r11d
000002d4      0 mov    r9d,DWORD PTR [rbx+r9*1]
000002d8      0 mov    r15d,DWORD PTR [rbp-0x38]
000002dc      0 test   r9d,r9d
000002df      0 je     0x364
000002e5      0 and    r12d,0xfff
000002ec      0 lea    r9d,[r9+r12*4]
000002f0      0 mov    edx,DWORD PTR [rbx+r9*1]
000002f4      0 test   edx,edx
000002f6      0 jne    0xc0
000002fc      0 jmp    0x369
00000301      0 mov    edx,DWORD PTR [rbx+r8*1+0x7d083a4]
00000309      0 mov    QWORD PTR [rbp-0x48],rax
0000030d      0 mov    rsi,QWORD PTR [rbp-0x10]
00000311      0 mov    eax,r9d
00000314      0 call   0xffffffffff38f240
00000319      0 test   eax,eax
0000031b      0 je     0x372
00000321      0 mov    ebx,eax
00000323      0 mov    r8,QWORD PTR [rbp-0x18]
00000327      0 mov    r9d,DWORD PTR [r8+rbx*1+0x28]
0000032c      0 mov    r11,QWORD PTR [rbp-0x20]
00000330      0 mov    DWORD PTR [r8+r11*1+0x354],r9d
00000338      0 mov    r9d,DWORD PTR [r8+rbx*1+0x2c]
0000033d      0 mov    DWORD PTR [r8+r11*1+0x358],r9d
00000345      0 mov    edx,DWORD PTR [r8+rbx*1+0x30]
0000034a      0 test   edx,edx
0000034c      0 je     0x37a
00000352      0 mov    rbx,r8
00000355      0 mov    r8,r11
00000358      0 jmp    0xc0
0000035d      0 xor    eax,eax
0000035f      0 mov    rsp,rbp
00000362      0 pop    rbp
00000363      0 ret
00000364      0 mov    rsp,rbp
00000367      0 pop    rbp
00000368      0 ret
00000369      0 mov    QWORD PTR [rbp-0x48],rax
0000036d      0 jmp    0x37a
00000372      0 mov    eax,DWORD PTR [rbp-0x48]
00000375      0 mov    rsp,rbp
00000378      0 pop    rbp
00000379      0 ret
0000037a      0 mov    eax,DWORD PTR [rbp-0x48]
0000037d      0 mov    rsp,rbp
00000380      0 pop    rbp
00000381      0 ret
00000382      0 cmp    DWORD PTR [rbp-0x28],0x0
00000386      0 jne    0x393
0000038c      0 xor    eax,eax
0000038e      0 mov    rsp,rbp
00000391      0 pop    rbp
00000392      0 ret
00000393      0 mov    QWORD PTR [rbp-0x38],rax
00000397      0 mov    edx,DWORD PTR [rbp-0x28]
0000039a      0 jmp    0x3c3
0000039f      0 nop    WORD PTR [rax+rax*1+0x0]
000003a8      0 nop    WORD PTR [rax+rax*1+0x0]
000003b1      0 nop    WORD PTR [rax+rax*1+0x0]
000003ba      0 nop    WORD PTR [rax+rax*1+0x0]
000003c0      0 mov    eax,DWORD PTR [rbp-0x38]
000003c3      0 mov    rsi,QWORD PTR [rbp-0x10]
000003c7      0 cmp    BYTE PTR [r13-0x4f],0x0
000003cc      0 jne    0xfa1
000003d2      0 mov    r8,QWORD PTR [rbp-0x18]
000003d6      0 mov    r9,QWORD PTR [rbp-0x20]
000003da      0 mov    DWORD PTR [r8+r9*1+0x35c],0x0
000003e6      0 mov    r11d,edx
000003e9      0 mov    r12d,DWORD PTR [rsi+0x7]
000003ed      0 or     r12,QWORD PTR [r13+0x288]
000003f4      0 cmp    DWORD PTR [r12+0x7],edx
000003f9      0 jbe    0x1074
000003ff      0 imul   r11,r11,0xc
00000403      0 mov    r15d,DWORD PTR [r12+r11*1+0x1f]
00000408      0 mov    edx,0x21
0000040d      0 cmp    edx,r15d
00000410      0 jne    0xfb9
00000416      0 mov    r15d,DWORD PTR [r12+r11*1+0x1b]
0000041b      0 mov    r11d,DWORD PTR [r12+r11*1+0x23]
00000420      0 or     r11,QWORD PTR [r13+0x288]
00000427      0 mov    rsi,r11
0000042a      0 mov    eax,DWORD PTR [rbp-0x30]
0000042d      0 mov    r10,QWORD PTR [rip+0xfffffffffffffcf3]        # 0x127
00000434      0 shl    r15d,0x9
00000438      0 shr    r15d,0x5
0000043c      0 cmp    QWORD PTR [r10+r15*1+0x8],0x8000001
00000445      0 je     0x448
00000447      0 int3
00000448      0 call   QWORD PTR [r10+r15*1]
0000044c      0 test   eax,eax
0000044e      0 je     0x6db
00000454      0 mov    rbx,QWORD PTR [rbp-0x18]
00000458      0 mov    r8,QWORD PTR [rbp-0x20]
0000045c      0 mov    r9,QWORD PTR [rbx+r8*1+0x348]
00000464      0 cmp    QWORD PTR [rbx+r8*1+0x348],0x0
0000046d      0 je     0x364
00000473      0 mov    r9d,DWORD PTR [rbx+r8*1+0x368]
0000047b      0 cmp    DWORD PTR [rbx+r8*1+0x368],0x0
00000484      0 jne    0x4a2
0000048a      0 movzx  r9d,BYTE PTR [rbx+r8*1+0x310]
00000493      0 test   BYTE PTR [rbx+r8*1+0x310],0x80
0000049c      0 je     0x364
000004a2      0 lea    r9,[rbx+0x259840]
000004a9      0 and    r9d,0x3
000004ad      0 mov    r9d,r9d
000004b0      0 test   r9d,r9d
000004b3      0 jne    0x106f
000004b9      0 mov    r9d,DWORD PTR [rbx+0x259840]
000004c0      0 test   r9d,r9d
000004c3      0 jne    0x364
000004c9      0 mov    r9d,DWORD PTR [rbx+r8*1+0x3c]
000004ce      0 mov    r9d,DWORD PTR [rbx+r8*1+0x33c]
000004d6      0 mov    r11d,0xfffffffc
000004dc      0 mov    r12d,0xfffffffe
000004e2      0 test   r9d,r9d
000004e5      0 cmovne r11d,r12d
000004e9      0 and    r11d,DWORD PTR [rbx+r8*1+0x3c]
000004ee      0 mov    DWORD PTR [rbx+r8*1+0x3c],r11d
000004f3      0 or     r9d,r11d
000004f6      0 cmp    BYTE PTR [rbx+0x262228],0x1
000004fd      0 jne    0x523
00000503      0 mov    r12d,DWORD PTR [rbx+0x2662c8]
0000050a      0 cmp    r11d,r12d
0000050d      0 jb     0x602
00000513      0 sub    r11d,r12d
00000516      0 cmp    DWORD PTR [rbx+0x2662cc],r11d
0000051d      0 jbe    0x602
00000523      0 mov    r11d,DWORD PTR [rbp-0x38]
00000527      0 mov    r12d,DWORD PTR [rbx+r11*1+0x8020]
0000052f      0 mov    r12d,r9d
00000532      0 sub    r12d,DWORD PTR [rbx+r11*1+0x8020]
0000053a      0 mov    r15d,DWORD PTR [rbx+r11*1+0x8024]
00000542      0 cmp    DWORD PTR [rbx+r11*1+0x8024],r12d
0000054a      0 ja     0x5bc
00000550      0 mov    r11d,r9d
00000553      0 shl    r11d,0x7
00000557      0 mov    r12d,r9d
0000055a      0 shr    r12d,1
0000055d      0 xor    r12d,r11d
00000560      0 and    r12d,0xfff
00000567      0 mov    r11d,DWORD PTR [rbp-0x38]
0000056b      0 lea    ecx,[r11+r12*8+0x14]
00000570      0 mov    edx,DWORD PTR [rbx+rcx*1+0x4]
00000574      0 test   edx,edx
00000576      0 je     0x58a
0000057c      0 mov    r12d,DWORD PTR [rbx+rcx*1]
00000580      0 cmp    DWORD PTR [rbx+rcx*1],r9d
00000584      0 je     0x5b4
0000058a      0 mov    QWORD PTR [rbp-0x28],rax
0000058e      0 mov    rsi,QWORD PTR [rbp-0x10]
00000592      0 mov    eax,r11d
00000595      0 mov    edx,r9d
00000598      0 call   0xffffffffff38f158
0000059d      0 test   eax,eax
0000059f      0 je     0x6e2
000005a5      0 mov    edx,eax
000005a7      0 mov    rbx,QWORD PTR [rbp-0x18]
000005ab      0 mov    r8,QWORD PTR [rbp-0x20]
000005af      0 jmp    0x3c0
000005b4      0 mov    r11d,edx
000005b7      0 jmp    0x6d3
000005bc      0 mov    r9d,DWORD PTR [rbx+r11*1+0x8014]
000005c4      0 mov    r11d,r12d
000005c7      0 shr    r11d,0xa
000005cb      0 and    r11d,0x3ffffc
000005d2      0 add    r9d,r11d
000005d5      0 mov    r9d,DWORD PTR [rbx+r9*1]
000005d9      0 mov    r15d,DWORD PTR [rbp-0x38]
000005dd      0 test   r9d,r9d
000005e0      0 je     0x364
000005e6      0 and    r12d,0xfff
000005ed      0 lea    r9d,[r9+r12*4]
000005f1      0 mov    edx,DWORD PTR [rbx+r9*1]
000005f5      0 test   edx,edx
000005f7      0 jne    0x3c0
000005fd      0 jmp    0x364
00000602      0 mov    edx,DWORD PTR [rbx+r8*1+0x7d083a4]
0000060a      0 mov    r11d,DWORD PTR [rbx+rdx*1+0x11c]
00000612      0 test   r11d,r11d
00000615      0 jne    0x623
0000061b      0 xor    r12d,r12d
0000061e      0 jmp    0x63b
00000623      0 lea    r12,[r11+rbx*1]
00000627      0 and    r12d,0x7
0000062b      0 mov    r12d,r12d
0000062e      0 test   r12d,r12d
00000631      0 jne    0x106a
00000637      0 mov    r12,QWORD PTR [rbx+r11*1]
0000063b      0 test   r12,r12
0000063e      0 je     0x6b0
00000644      0 mov    r15d,DWORD PTR [rbx+0x266238]
0000064b      0 mov    ecx,DWORD PTR [rbx+rdx*1+0x120]
00000652      0 mov    esi,r9d
00000655      0 shl    esi,0x7
00000658      0 mov    edi,r9d
0000065b      0 shr    edi,1
0000065d      0 xor    edi,esi
0000065f      0 xor    edi,ecx
00000661      0 and    edi,0xfff
00000667      0 imul   esi,edi,0x18
0000066a      0 add    r15d,esi
0000066d      0 mov    rsi,QWORD PTR [rbx+r15*1]
00000671      0 shl    rcx,0x20
00000675      0 or     rcx,r9
00000678      0 cmp    QWORD PTR [rbx+r15*1],rcx
0000067c      0 jne    0x6b0
00000682      0 mov    rcx,QWORD PTR [rbx+r15*1+0x8]
00000687      0 cmp    QWORD PTR [rbx+r15*1+0x8],r12
0000068c      0 jne    0x6b0
00000692      0 mov    r12d,DWORD PTR [rbx+r15*1+0x10]
00000697      0 cmp    DWORD PTR [rbx+r15*1+0x10],r11d
0000069c      0 jne    0x6b0
000006a2      0 mov    r11d,DWORD PTR [rbx+r15*1+0x14]
000006a7      0 test   r11d,r11d
000006aa      0 jne    0x6d3
000006b0      0 mov    QWORD PTR [rbp-0x28],rax
000006b4      0 mov    rsi,QWORD PTR [rbp-0x10]
000006b8      0 mov    eax,r9d
000006bb      0 call   0xffffffffff38f250
000006c0      0 test   eax,eax
000006c2      0 je     0x6ea
000006c8      0 mov    r11d,eax
000006cb      0 mov    rbx,QWORD PTR [rbp-0x18]
000006cf      0 mov    r8,QWORD PTR [rbp-0x20]
000006d3      0 mov    edx,r11d
000006d6      0 jmp    0x3c0
000006db      0 xor    eax,eax
000006dd      0 mov    rsp,rbp
000006e0      0 pop    rbp
000006e1      0 ret
000006e2      0 mov    eax,DWORD PTR [rbp-0x28]
000006e5      0 mov    rsp,rbp
000006e8      0 pop    rbp
000006e9      0 ret
000006ea      0 mov    eax,DWORD PTR [rbp-0x28]
000006ed      0 mov    rsp,rbp
000006f0      0 pop    rbp
000006f1      0 ret
000006f2      0 mov    r12d,DWORD PTR [rbx+r11*1+0x108]
000006fa      0 cmp    r8d,0x3
000006fe      0 je     0xa44
00000704      0 test   r12d,r12d
00000707      0 je     0x71d
0000070d      0 mov    r8d,DWORD PTR [rbx+r11*1+0x110]
00000715      0 mov    DWORD PTR [rbx+r9*1+0x350],r8d
0000071d      0 call   0xffffffffff38f160
00000722      0 cmp    DWORD PTR [rbp-0x28],0x0
00000726      0 jne    0x733
0000072c      0 xor    eax,eax
0000072e      0 mov    rsp,rbp
00000731      0 pop    rbp
00000732      0 ret
00000733      0 mov    QWORD PTR [rbp-0x38],rax
00000737      0 mov    edx,DWORD PTR [rbp-0x28]
0000073a      0 mov    r8,QWORD PTR [rbp-0x20]
0000073e      0 mov    rbx,QWORD PTR [rbp-0x18]
00000742      0 jmp    0x783
00000747      0 nop    WORD PTR [rax+rax*1+0x0]
00000750      0 nop    WORD PTR [rax+rax*1+0x0]
00000759      0 nop    WORD PTR [rax+rax*1+0x0]
00000762      0 nop    WORD PTR [rax+rax*1+0x0]
0000076b      0 nop    WORD PTR [rax+rax*1+0x0]
00000774      0 nop    WORD PTR [rax+rax*1+0x0]
0000077d      0 nop    DWORD PTR [rax]
00000780      0 mov    eax,r11d
00000783      0 mov    rsi,QWORD PTR [rbp-0x10]
00000787      0 cmp    BYTE PTR [r13-0x4f],0x0
0000078c      0 jne    0xfc8
00000792      0 mov    DWORD PTR [rbx+r8*1+0x35c],0x0
0000079e      0 mov    r9d,edx
000007a1      0 mov    r11d,DWORD PTR [rsi+0x7]
000007a5      0 or     r11,QWORD PTR [r13+0x288]
000007ac      0 cmp    DWORD PTR [r11+0x7],edx
000007b0      0 jbe    0x1065
000007b6      0 imul   r9,r9,0xc
000007ba      0 mov    r12d,DWORD PTR [r11+r9*1+0x1f]
000007bf      0 mov    r15d,0x21
000007c5      0 cmp    r15d,r12d
000007c8      0 jne    0xfe8
000007ce      0 mov    r12d,DWORD PTR [r11+r9*1+0x1b]
000007d3      0 mov    r9d,DWORD PTR [r11+r9*1+0x23]
000007d8      0 or     r9,QWORD PTR [r13+0x288]
000007df      0 mov    rsi,r9
000007e2      0 mov    eax,DWORD PTR [rbp-0x30]
000007e5      0 mov    r10,QWORD PTR [rip+0xfffffffffffff93b]        # 0x127
000007ec      0 shl    r12d,0x9
000007f0      0 shr    r12d,0x5
000007f4      0 cmp    QWORD PTR [r10+r12*1+0x8],0x8000001
000007fd      0 je     0x800
000007ff      0 int3
00000800      0 call   QWORD PTR [r10+r12*1]
00000804      0 test   eax,eax
00000806      0 je     0xa25
0000080c      0 mov    rbx,QWORD PTR [rbp-0x18]
00000810      0 mov    r8,QWORD PTR [rbp-0x20]
00000814      0 mov    r9,QWORD PTR [rbx+r8*1+0x348]
0000081c      0 cmp    QWORD PTR [rbx+r8*1+0x348],0x0
00000825      0 je     0x364
0000082b      0 mov    r9d,DWORD PTR [rbx+r8*1+0x368]
00000833      0 cmp    DWORD PTR [rbx+r8*1+0x368],0x0
0000083c      0 jne    0x85a
00000842      0 movzx  r9d,BYTE PTR [rbx+r8*1+0x310]
0000084b      0 test   BYTE PTR [rbx+r8*1+0x310],0x80
00000854      0 je     0x364
0000085a      0 lea    r9,[rbx+0x259840]
00000861      0 and    r9d,0x3
00000865      0 mov    r9d,r9d
00000868      0 test   r9d,r9d
0000086b      0 jne    0x1060
00000871      0 mov    r9d,DWORD PTR [rbx+0x259840]
00000878      0 test   r9d,r9d
0000087b      0 jne    0x364
00000881      0 mov    r9d,DWORD PTR [rbx+r8*1+0x3c]
00000886      0 mov    r9d,DWORD PTR [rbx+r8*1+0x33c]
0000088e      0 mov    r11d,0xfffffffc
00000894      0 mov    r12d,0xfffffffe
0000089a      0 test   r9d,r9d
0000089d      0 cmovne r11d,r12d
000008a1      0 and    r11d,DWORD PTR [rbx+r8*1+0x3c]
000008a6      0 mov    DWORD PTR [rbx+r8*1+0x3c],r11d
000008ab      0 or     r9d,r11d
000008ae      0 cmp    BYTE PTR [rbx+0x262228],0x1
000008b5      0 jne    0x8db
000008bb      0 mov    r12d,DWORD PTR [rbx+0x2662c8]
000008c2      0 cmp    r11d,r12d
000008c5      0 jb     0x9c5
000008cb      0 sub    r11d,r12d
000008ce      0 cmp    DWORD PTR [rbx+0x2662cc],r11d
000008d5      0 jbe    0x9c5
000008db      0 mov    QWORD PTR [rbx+r8*1+0x354],0x0
000008e7      0 mov    r11d,DWORD PTR [rbp-0x38]
000008eb      0 mov    r12d,DWORD PTR [rbx+r11*1+0x8020]
000008f3      0 mov    r12d,r9d
000008f6      0 sub    r12d,DWORD PTR [rbx+r11*1+0x8020]
000008fe      0 mov    r15d,DWORD PTR [rbx+r11*1+0x8024]
00000906      0 cmp    DWORD PTR [rbx+r11*1+0x8024],r12d
0000090e      0 ja     0x97c
00000914      0 mov    r11d,r9d
00000917      0 shl    r11d,0x7
0000091b      0 mov    r12d,r9d
0000091e      0 shr    r12d,1
00000921      0 xor    r12d,r11d
00000924      0 and    r12d,0xfff
0000092b      0 mov    r11d,DWORD PTR [rbp-0x38]
0000092f      0 lea    ecx,[r11+r12*8+0x14]
00000934      0 mov    edx,DWORD PTR [rbx+rcx*1+0x4]
00000938      0 test   edx,edx
0000093a      0 je     0x94e
00000940      0 mov    r12d,DWORD PTR [rbx+rcx*1]
00000944      0 cmp    DWORD PTR [rbx+rcx*1],r9d
00000948      0 je     0x780
0000094e      0 mov    QWORD PTR [rbp-0x28],rax
00000952      0 mov    rsi,QWORD PTR [rbp-0x10]
00000956      0 mov    eax,r11d
00000959      0 mov    edx,r9d
0000095c      0 call   0xffffffffff38f158
00000961      0 test   eax,eax
00000963      0 je     0xa2c
00000969      0 mov    edx,eax
0000096b      0 mov    rbx,QWORD PTR [rbp-0x18]
0000096f      0 mov    r8,QWORD PTR [rbp-0x20]
00000973      0 mov    r11d,DWORD PTR [rbp-0x38]
00000977      0 jmp    0x780
0000097c      0 mov    r9d,DWORD PTR [rbx+r11*1+0x8014]
00000984      0 mov    r11d,r12d
00000987      0 shr    r11d,0xa
0000098b      0 and    r11d,0x3ffffc
00000992      0 add    r9d,r11d
00000995      0 mov    r9d,DWORD PTR [rbx+r9*1]
00000999      0 mov    r15d,DWORD PTR [rbp-0x38]
0000099d      0 test   r9d,r9d
000009a0      0 je     0x364
000009a6      0 and    r12d,0xfff
000009ad      0 lea    r9d,[r9+r12*4]
000009b1      0 mov    edx,DWORD PTR [rbx+r9*1]
000009b5      0 test   edx,edx
000009b7      0 je     0x364
000009bd      0 mov    r11d,r15d
000009c0      0 jmp    0x780
000009c5      0 mov    edx,DWORD PTR [rbx+r8*1+0x7d083a4]
000009cd      0 mov    QWORD PTR [rbp-0x28],rax
000009d1      0 mov    rsi,QWORD PTR [rbp-0x10]
000009d5      0 mov    eax,r9d
000009d8      0 call   0xffffffffff38f240
000009dd      0 test   eax,eax
000009df      0 je     0xa34
000009e5      0 mov    ebx,eax
000009e7      0 mov    r8,QWORD PTR [rbp-0x18]
000009eb      0 mov    r9d,DWORD PTR [r8+rbx*1+0x28]
000009f0      0 mov    r15,QWORD PTR [rbp-0x20]
000009f4      0 mov    DWORD PTR [r8+r15*1+0x354],r9d
000009fc      0 mov    r9d,DWORD PTR [r8+rbx*1+0x2c]
00000a01      0 mov    DWORD PTR [r8+r15*1+0x358],r9d
00000a09      0 mov    edx,DWORD PTR [r8+rbx*1+0x30]
00000a0e      0 test   edx,edx
00000a10      0 je     0xa3c
00000a16      0 mov    r11d,DWORD PTR [rbp-0x38]
00000a1a      0 mov    rbx,r8
00000a1d      0 mov    r8,r15
00000a20      0 jmp    0x780
00000a25      0 xor    eax,eax
00000a27      0 mov    rsp,rbp
00000a2a      0 pop    rbp
00000a2b      0 ret
00000a2c      0 mov    eax,DWORD PTR [rbp-0x28]
00000a2f      0 mov    rsp,rbp
00000a32      0 pop    rbp
00000a33      0 ret
00000a34      0 mov    eax,DWORD PTR [rbp-0x28]
00000a37      0 mov    rsp,rbp
00000a3a      0 pop    rbp
00000a3b      0 ret
00000a3c      0 mov    eax,DWORD PTR [rbp-0x28]
00000a3f      0 mov    rsp,rbp
00000a42      0 pop    rbp
00000a43      0 ret
00000a44      0 test   r12d,r12d
00000a47      0 je     0xa5d
00000a4d      0 mov    r8d,DWORD PTR [rbx+r11*1+0x110]
00000a55      0 mov    DWORD PTR [rbx+r9*1+0x350],r8d
00000a5d      0 movzx  r8d,BYTE PTR [rbx+0x261928]
00000a65      0 test   r8b,0x1
00000a69      0 jne    0xc1c
00000a6f      0 mov    eax,0x261928
00000a74      0 mov    r8,rax
00000a77      0 call   0xffffffffff3a9298
00000a7c      0 test   eax,eax
00000a7e      0 je     0xc1c
00000a84      0 mov    rbx,QWORD PTR [rbp-0x18]
00000a88      0 mov    QWORD PTR [rbx+0x259908],0x0
00000a93      0 mov    QWORD PTR [rbx+0x259900],0x0
00000a9e      0 mov    DWORD PTR [rbx+0x259910],0x3f800000
00000aa8      0 mov    rsi,QWORD PTR [rbp-0x10]
00000aac      0 mov    r8,QWORD PTR [rsi+0x1f]
00000ab0      0 lea    r9,[r8-0x8014]
00000ab7      0 cmp    r9,0x259914
00000abe      0 setae  r9b
00000ac2      0 movzx  r9d,r9b
00000ac6      0 cmp    r8,0x8014
00000acd      0 setae  r8b
00000ad1      0 movzx  r8d,r8b
00000ad5      0 test   r9d,r8d
00000ad8      0 je     0x105b
00000ade      0 lea    rdi,[rbx+0x259914]
00000ae5      0 mov    r10,rsp
00000ae8      0 sub    rsp,0x8
00000aec      0 and    rsp,0xfffffffffffffff0
00000af0      0 mov    QWORD PTR [rsp],r10
00000af4      0 mov    edx,0x8014
00000af9      0 xor    esi,esi
00000afb      0 movabs rax,0x55a7535d9a20
00000b05      0 lea    r10,[rip+0x10]        # 0xb1c
00000b0c      0 mov    QWORD PTR [r13+0xa8],r10
00000b13      0 mov    QWORD PTR [r13+0xb0],rbp
00000b1a      0 call   rax
00000b1c      0 mov    rsp,QWORD PTR [rsp]
00000b20      0 mov    QWORD PTR [r13+0xb0],0x0
00000b2b      0 mov    rsi,QWORD PTR [rbp-0x10]
00000b2f      0 mov    ebx,DWORD PTR [rsi+0x57]
00000b32      0 add    rbx,r14
00000b35      0 mov    r8d,DWORD PTR [rbx+0x7]
00000b39      0 sub    r8d,0x20
00000b3d      0 mov    DWORD PTR [rbx+0x7],r8d
00000b41      0 lea    eax,[r8+0x8]
00000b45      0 mov    QWORD PTR [rbp-0x40],rbx
00000b49      0 mov    QWORD PTR [rbp-0x48],r8
00000b4d      0 mov    edx,0x261928
00000b52      0 call   0xffffffffff3a92a0
00000b57      0 mov    ebx,eax
00000b59      0 mov    r8,QWORD PTR [rbp-0x18]
00000b5d      0 mov    r9d,DWORD PTR [r8+rbx*1]
00000b61      0 mov    r11d,0x1
00000b67      0 mov    r10,r11
00000b6a      0 xchg   BYTE PTR [r8+r9*1],r10b
00000b6e      0 mov    r9,QWORD PTR [rbp-0x40]
00000b72      0 mov    r11d,DWORD PTR [r9+0x7]
00000b76      0 lea    edx,[r11-0x10]
00000b7a      0 mov    DWORD PTR [r9+0x7],edx
00000b7e      0 lea    eax,[rdx+0xc]
00000b81      0 mov    QWORD PTR [rbp-0x50],rbx
00000b85      0 mov    QWORD PTR [rbp-0x38],rdx
00000b89      0 mov    edx,0x21f5b
00000b8e      0 mov    rsi,QWORD PTR [rbp-0x10]
00000b92      0 mov    r11,rdx
00000b95      0 call   0xffffffffff3a92b0
00000b9a      0 mov    rbx,QWORD PTR [rbp-0x18]
00000b9e      0 mov    r8,QWORD PTR [rbp-0x50]
00000ba2      0 mov    r8d,DWORD PTR [rbx+r8*1+0x4]
00000ba7      0 movzx  r9d,BYTE PTR [rbx+r8*1]
00000bac      0 mov    BYTE PTR [rbx+r8*1],0x1
00000bb1      0 mov    QWORD PTR [rbp-0x50],r9
00000bb5      0 mov    rsi,QWORD PTR [rbp-0x10]
00000bb9      0 call   0xffffffffff3a92b8
00000bbe      0 test   BYTE PTR [rbp-0x50],0x4
00000bc2      0 je     0xbde
00000bc8      0 mov    eax,0x2f3234
00000bcd      0 mov    rsi,QWORD PTR [rbp-0x10]
00000bd1      0 call   0xffffffffff3a81e8
00000bd6      0 test   eax,eax
00000bd8      0 jne    0xbfb
00000bde      0 mov    edx,DWORD PTR [rbp-0x38]
00000be1      0 lea    ebx,[rdx+0x10]
00000be4      0 mov    r8,QWORD PTR [rbp-0x40]
00000be8      0 mov    DWORD PTR [r8+0x7],ebx
00000bec      0 mov    ebx,DWORD PTR [rbp-0x48]
00000bef      0 add    ebx,0x20
00000bf2      0 mov    DWORD PTR [r8+0x7],ebx
00000bf6      0 jmp    0xc1c
00000bfb      0 mov    edx,DWORD PTR [rbp-0x38]
00000bfe      0 mov    rbx,QWORD PTR [rbp-0x18]
00000c02      0 mov    DWORD PTR [rbx+rdx*1],0x21f5b
00000c09      0 mov    eax,0xb79e
00000c0e      0 mov    rsi,QWORD PTR [rbp-0x10]
00000c12      0 call   0xffffffffff3a9290
00000c17      0 jmp    0x1056
00000c1c      0 cmp    DWORD PTR [rbp-0x28],0x0
00000c20      0 jne    0xc2d
00000c26      0 xor    eax,eax
00000c28      0 mov    rsp,rbp
00000c2b      0 pop    rbp
00000c2c      0 ret
00000c2d      0 mov    edx,DWORD PTR [rbp-0x28]
00000c30      0 mov    rbx,QWORD PTR [rbp-0x20]
00000c34      1 jmp    0xc43
00000c39      0 nop    DWORD PTR [rax+0x0]
00000c40    108 mov    rbx,r8
00000c43      2 mov    rsi,QWORD PTR [rbp-0x10]
00000c47      0 cmp    BYTE PTR [r13-0x4f],0x0
00000c4c     19 jne    0xff7
00000c52    105 mov    r8,QWORD PTR [rbp-0x18]
00000c56      0 mov    DWORD PTR [r8+rbx*1+0x35c],0x0
00000c62     35 mov    r9d,edx
00000c65     15 mov    r11d,DWORD PTR [rsi+0x7]
00000c69     92 or     r11,QWORD PTR [r13+0x288]
00000c70      6 cmp    DWORD PTR [r11+0x7],edx
00000c74      0 jbe    0x1051
00000c7a     84 imul   r9,r9,0xc
00000c7e     17 mov    r12d,DWORD PTR [r11+r9*1+0x1f]
00000c83    470 mov    r15d,0x21
00000c89      1 cmp    r15d,r12d
00000c8c      0 jne    0x1010
00000c92     92 mov    r12d,DWORD PTR [r11+r9*1+0x1b]
00000c97     25 mov    r9d,DWORD PTR [r11+r9*1+0x23]
00000c9c     24 or     r9,QWORD PTR [r13+0x288]
00000ca3    106 mov    rsi,r9
00000ca6     12 mov    eax,DWORD PTR [rbp-0x30]
00000ca9      6 mov    r10,QWORD PTR [rip+0xfffffffffffff477]        # 0x127
00000cb0     15 shl    r12d,0x9
00000cb4     85 shr    r12d,0x5
00000cb8     11 cmp    QWORD PTR [r10+r12*1+0x8],0x8000001
00000cc1    786 je     0xcc4
00000cc3      0 int3
00000cc4    114 call   QWORD PTR [r10+r12*1]
00000cc8     43 test   eax,eax
00000cca      0 je     0xf3d
00000cd0     14 mov    rbx,QWORD PTR [rbp-0x18]
00000cd4     46 mov    r8,QWORD PTR [rbp-0x20]
00000cd8     57 mov    r9,QWORD PTR [rbx+r8*1+0x348]
00000ce0    107 cmp    QWORD PTR [rbx+r8*1+0x348],0x0
00000ce9     60 je     0x364
00000cef     73 mov    r9d,DWORD PTR [rbx+r8*1+0x368]
00000cf7     20 cmp    DWORD PTR [rbx+r8*1+0x368],0x0
00000d00     36 jne    0xd1e
00000d06      0 movzx  r9d,BYTE PTR [rbx+r8*1+0x310]
00000d0f      0 test   BYTE PTR [rbx+r8*1+0x310],0x80
00000d18      0 je     0x364
00000d1e     67 lea    r9,[rbx+0x259840]
00000d25     32 and    r9d,0x3
00000d29     10 mov    r9d,r9d
00000d2c     31 test   r9d,r9d
00000d2f      0 jne    0x104c
00000d35     57 mov    r9d,DWORD PTR [rbx+0x259840]
00000d3c     23 test   r9d,r9d
00000d3f      0 jne    0x364
00000d45     15 mov    r9d,DWORD PTR [rbx+r8*1+0x3c]
00000d4a     55 mov    r9d,DWORD PTR [rbx+r8*1+0x33c]
00000d52     74 mov    r11d,0xfffffffc
00000d58     33 mov    r12d,0xfffffffe
00000d5e     18 test   r9d,r9d
00000d61     29 cmovne r11d,r12d
00000d65     63 and    r11d,DWORD PTR [rbx+r8*1+0x3c]
00000d6a     50 mov    DWORD PTR [rbx+r8*1+0x3c],r11d
00000d6f     61 or     r9d,r11d
00000d72     28 cmp    BYTE PTR [rbx+0x262228],0x1
00000d79     31 jne    0xd9f
00000d7f     31 mov    r12d,DWORD PTR [rbx+0x2662c8]
00000d86     34 cmp    r11d,r12d
00000d89      0 jb     0xe64
00000d8f      0 sub    r11d,r12d
00000d92      0 cmp    DWORD PTR [rbx+0x2662cc],r11d
00000d99      0 jbe    0xe64
00000d9f      0 mov    r11d,r9d
00000da2      0 sub    r11d,DWORD PTR [rbx+0x261920]
00000da9      0 cmp    DWORD PTR [rbx+0x261924],r11d
00000db0      0 ja     0xe23
00000db6      0 mov    r11d,r9d
00000db9      0 shl    r11d,0x7
00000dbd      0 mov    r12d,r9d
00000dc0      0 shr    r12d,1
00000dc3      0 xor    r12d,r11d
00000dc6      0 and    r12d,0xfff
00000dcd      0 lea    ecx,[r12*8+0x259914]
00000dd5      0 mov    edx,DWORD PTR [rbx+rcx*1+0x4]
00000dd9      0 test   edx,edx
00000ddb      0 je     0xdef
00000de1      0 mov    r11d,DWORD PTR [rbx+rcx*1]
00000de5      0 cmp    DWORD PTR [rbx+rcx*1],r9d
00000de9      0 je     0xe1b
00000def      0 mov    QWORD PTR [rbp-0x28],rax
00000df3      0 mov    rsi,QWORD PTR [rbp-0x10]
00000df7      0 mov    eax,0x259900
00000dfc      0 mov    edx,r9d
00000dff      0 call   0xffffffffff38f158
00000e04      0 test   eax,eax
00000e06      0 je     0xf44
00000e0c      0 mov    edx,eax
00000e0e      0 mov    rbx,QWORD PTR [rbp-0x18]
00000e12      0 mov    r8,QWORD PTR [rbp-0x20]
00000e16      0 jmp    0xc40
00000e1b      0 mov    r11d,edx
00000e1e      0 jmp    0xf35
00000e23      1 mov    r9d,DWORD PTR [rbx+0x261914]
00000e2a      0 mov    r12d,r11d
00000e2d      0 shr    r12d,0xa
00000e31      1 and    r12d,0x3ffffc
00000e38      0 add    r9d,r12d
00000e3b      0 mov    r9d,DWORD PTR [rbx+r9*1]
00000e3f      5 test   r9d,r9d
00000e42      0 je     0x364
00000e48      0 and    r11d,0xfff
00000e4f      0 lea    r9d,[r9+r11*4]
00000e53      0 mov    edx,DWORD PTR [rbx+r9*1]
00000e57     10 test   edx,edx
00000e59      0 jne    0xc40
00000e5f      0 jmp    0x364
00000e64     22 mov    edx,DWORD PTR [rbx+r8*1+0x7d083a4]
00000e6c     26 mov    r11d,DWORD PTR [rbx+rdx*1+0x11c]
00000e74     82 test   r11d,r11d
00000e77      0 jne    0xe85
00000e7d      0 xor    r12d,r12d
00000e80      0 jmp    0xe9d
00000e85     71 lea    r12,[r11+rbx*1]
00000e89      7 and    r12d,0x7
00000e8d     89 mov    r12d,r12d
00000e90     33 test   r12d,r12d
00000e93      0 jne    0x1047
00000e99     76 mov    r12,QWORD PTR [rbx+r11*1]
00000e9d     32 test   r12,r12
00000ea0      0 je     0xf12
00000ea6     64 mov    r15d,DWORD PTR [rbx+0x266238]
00000ead     36 mov    ecx,DWORD PTR [rbx+rdx*1+0x120]
00000eb4     51 mov    esi,r9d
00000eb7      1 shl    esi,0x7
00000eba     64 mov    edi,r9d
00000ebd     27 shr    edi,1
00000ebf     51 xor    edi,esi
00000ec1      1 xor    edi,ecx
00000ec3     70 and    edi,0xfff
00000ec9     41 imul   esi,edi,0x18
00000ecc     48 add    r15d,esi
00000ecf     12 mov    rsi,QWORD PTR [rbx+r15*1]
00000ed3    418 shl    rcx,0x20
00000ed7     21 or     rcx,r9
00000eda     27 cmp    QWORD PTR [rbx+r15*1],rcx
00000ede      1 jne    0xf12
00000ee4    137 mov    rcx,QWORD PTR [rbx+r15*1+0x8]
00000ee9     40 cmp    QWORD PTR [rbx+r15*1+0x8],r12
00000eee      0 jne    0xf12
00000ef4     80 mov    r12d,DWORD PTR [rbx+r15*1+0x10]
00000ef9     38 cmp    DWORD PTR [rbx+r15*1+0x10],r11d
00000efe      0 jne    0xf12
00000f04    108 mov    r11d,DWORD PTR [rbx+r15*1+0x14]
00000f09     11 test   r11d,r11d
00000f0c      0 jne    0xf35
00000f12      4 mov    QWORD PTR [rbp-0x28],rax
00000f16      4 mov    rsi,QWORD PTR [rbp-0x10]
00000f1a      0 mov    eax,r9d
00000f1d      0 call   0xffffffffff38f250
00000f22      0 test   eax,eax
00000f24      0 je     0xf4c
00000f2a      0 mov    r11d,eax
00000f2d      0 mov    rbx,QWORD PTR [rbp-0x18]
00000f31      0 mov    r8,QWORD PTR [rbp-0x20]
00000f35     90 mov    edx,r11d
00000f38     16 jmp    0xc40
00000f3d      0 xor    eax,eax
00000f3f      0 mov    rsp,rbp
00000f42      0 pop    rbp
00000f43      0 ret
00000f44      0 mov    eax,DWORD PTR [rbp-0x28]
00000f47      0 mov    rsp,rbp
00000f4a      0 pop    rbp
00000f4b      0 ret
00000f4c      0 mov    eax,DWORD PTR [rbp-0x28]
00000f4f      0 mov    rsp,rbp
00000f52      0 pop    rbp
00000f53      0 ret
00000f54      0 xor    ebx,ebx
00000f56      0 sar    ebx,1
00000f58      0 movsxd rbx,ebx
00000f5b      0 mov    rax,rbx
00000f5e      0 call   0xffffffffff3a9770
00000f63      0 mov    rsi,QWORD PTR [rbp-0x10]
00000f67      0 mov    eax,DWORD PTR [rbp-0x30]
00000f6a      0 mov    edx,DWORD PTR [rbp-0x28]
00000f6d      0 jmp    0x1d
00000f72      0 mov    QWORD PTR [rbp-0x28],rdx
00000f76      0 call   0xffffffffff3a9780
00000f7b      0 mov    rsi,QWORD PTR [rbp-0x10]
00000f7f      0 mov    rbx,QWORD PTR [rbp-0x18]
00000f83      0 mov    r8,QWORD PTR [rbp-0x20]
00000f87      0 mov    edx,DWORD PTR [rbp-0x28]
00000f8a      0 mov    eax,DWORD PTR [rbp-0x38]
00000f8d      0 jmp    0xd2
00000f92      0 cmp    r12d,0xffffffff
00000f96      0 je     0x1042
00000f9c      0 jmp    0x103d
00000fa1      0 mov    QWORD PTR [rbp-0x28],rdx
00000fa5      0 call   0xffffffffff3a9780
00000faa      0 mov    rsi,QWORD PTR [rbp-0x10]
00000fae      0 mov    eax,DWORD PTR [rbp-0x38]
00000fb1      0 mov    edx,DWORD PTR [rbp-0x28]
00000fb4      0 jmp    0x3d2
00000fb9      0 cmp    r15d,0xffffffff
00000fbd      0 je     0x1038
00000fc3      0 jmp    0x1033
00000fc8      0 mov    QWORD PTR [rbp-0x28],rdx
00000fcc      0 call   0xffffffffff3a9780
00000fd1      0 mov    rsi,QWORD PTR [rbp-0x10]
00000fd5      0 mov    rbx,QWORD PTR [rbp-0x18]
00000fd9      0 mov    r8,QWORD PTR [rbp-0x20]
00000fdd      0 mov    edx,DWORD PTR [rbp-0x28]
00000fe0      0 mov    eax,DWORD PTR [rbp-0x38]
00000fe3      0 jmp    0x792
00000fe8      0 cmp    r12d,0xffffffff
00000fec      0 je     0x102e
00000ff2      0 jmp    0x1029
00000ff7      0 mov    QWORD PTR [rbp-0x28],rdx
00000ffb      0 call   0xffffffffff3a9780
00001000      0 mov    rsi,QWORD PTR [rbp-0x10]
00001004      0 mov    edx,DWORD PTR [rbp-0x28]
00001007      0 mov    rbx,QWORD PTR [rbp-0x20]
0000100b      0 jmp    0xc52
00001010      0 cmp    r12d,0xffffffff
00001014      0 je     0x1024
0000101a      0 jmp    0x101f
0000101f      0 call   0xffffffffff3a94c0
00001024      0 call   0xffffffffff3a94b0
00001029      0 call   0xffffffffff3a94c0
0000102e      0 call   0xffffffffff3a94b0
00001033      0 call   0xffffffffff3a94c0
00001038      0 call   0xffffffffff3a94b0
0000103d      0 call   0xffffffffff3a94c0
00001042      0 call   0xffffffffff3a94b0
00001047      0 call   0xffffffffff3a9460
0000104c      0 call   0xffffffffff3a9460
00001051      0 call   0xffffffffff3a94f0
00001056      0 call   0xffffffffff3a9440
0000105b      0 call   0xffffffffff3a9450
00001060      0 call   0xffffffffff3a9460
00001065      0 call   0xffffffffff3a94f0
0000106a      0 call   0xffffffffff3a9460
0000106f      0 call   0xffffffffff3a9460
00001074      0 call   0xffffffffff3a94f0
00001079      0 call   0xffffffffff3a9460
0000107e      0 call   0xffffffffff3a94f0
00001083      0 nop
00001084      0 or     al,0x0
00001086      0 add    BYTE PTR [rax],al
00001088      0 (bad)
00001089      0 add    BYTE PTR [rax],al
0000108b      0 add    dh,dl
0000108d      0 (bad)
0000108e      0 adc    eax,eax
00001090      0 add    DWORD PTR [rcx],edx
00001092      0 int3
00001093      0 int3
