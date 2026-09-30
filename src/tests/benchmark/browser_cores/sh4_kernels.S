.text
.global _start
_start:
#if WORKLOAD == 0
1: mov r0,r6
 mov #13,r7
 shld r7,r6
 xor r6,r0
 mov r0,r6
 mov #-17,r7
 shld r7,r6
 xor r6,r0
 mov r0,r6
 mov #5,r7
 shld r7,r6
 xor r6,r0
 add r0,r3
 mov r3,r4
 mov #-7,r7
 shld r7,r4
 mov r3,r6
 mov #25,r7
 shld r7,r6
 or r6,r4
 xor r4,r0
#elif WORKLOAD == 1
1: mov r1,r4
 extu.b r4,r4
 mov r4,r6
 shll2 r6
 add r2,r6
 mov.l @r6,r5
 add r5,r0
 mov r0,r5
 mov #-11,r7
 shld r7,r5
 mov r0,r8
 mov #21,r7
 shld r7,r8
 or r8,r5
 xor r0,r5
 mov.l r5,@r6
 add r5,r3
#elif WORKLOAD == 2
1: tst #1,r0
 bt 2f
 xor r0,r3
 bra 3f
 nop
2: add r0,r3
3: mov r0,r4
 rotr r4
 mov r4,r0
 xor r3,r0
 cmp/hs r3,r0
 bt 4f
 add #31,r0
 bra 5f
 nop
4: add #-7,r0
5:
#else
1: mov r2,r6
 add r4,r6
 mov.l @r6,r4
 add r4,r0
 mov r0,r6
 mov #-9,r7
 shld r7,r6
 mov r0,r8
 mov #23,r7
 shld r7,r8
 or r8,r6
 xor r6,r3
 mov r2,r6
 add r4,r6
 mov.l @r6,r5
 add r5,r0
 mov r5,r4
#endif
 dt r1
 bf 1b
 mov #1,r10
6: bra 6b
 nop
