.syntax unified
.arch armv4t
.arm
.global _start
_start:
#if WORKLOAD == 0
1: eor r0, r0, r0, lsl #13
 eor r0, r0, r0, lsr #17
 eor r0, r0, r0, lsl #5
 add r3, r3, r0
 mov r4, r3, ror #7
 eor r0, r0, r4
 subs r1, r1, #1
 bne 1b
#elif WORKLOAD == 1
1: and r4, r1, #255
 ldr r5, [r2, r4, lsl #2]
 add r0, r0, r5
 eor r5, r0, r0, ror #11
 str r5, [r2, r4, lsl #2]
 add r3, r3, r5
 subs r1, r1, #1
 bne 1b
#elif WORKLOAD == 2
1: tst r0, #1
 addeq r3, r3, r0
 eorne r3, r3, r0
 mov r4, r0, ror #1
 eor r0, r4, r3
 cmp r0, r3
 addlo r0, r0, #31
 subhs r0, r0, #7
 subs r1, r1, #1
 bne 1b
#else
1: ldr r4, [r2, r4]
 add r0, r0, r4
 eor r3, r3, r0, ror #9
 ldr r5, [r2, r4]
 add r0, r0, r5
 mov r4, r5
 subs r1, r1, #1
 bne 1b
#endif
 mov r10, #1
2: b 2b
