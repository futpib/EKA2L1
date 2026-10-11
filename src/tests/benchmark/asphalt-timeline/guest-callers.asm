
; Asphalt decompressed code, guest offset 0xaaa
70000aaa push     {r3, r4, r5, lr}
70000aac movs     r4, r0
70000aae adds     r4, #0xff
70000ab0 adds     r4, #0x41
70000ab2 ldr      r0, [r4, #0xc]
70000ab4 bl       #0x70011718
70000ab8 ldr      r2, [pc, #0x130]
70000aba ldr      r1, [r4, #0xc]
70000abc movs     r0, #0
70000abe adds     r1, r1, r2
70000ac0 strb     r0, [r1, #0xe]
70000ac2 str      r0, [sp]
70000ac4 blx      #0x70033048
70000ac8 movs     r0, #1
70000aca pop      {r3, r4, r5, pc}
70000acc push     {r4, lr}
70000ace bl       #0x7001f10e

; Asphalt decompressed code, guest offset 0x19b6
700019b6 movs     r1, #0x57
700019b8 lsls     r1, r1, #6
700019ba adds     r1, r6, r1
700019bc movs     r3, #0x4b
700019be ldr      r2, [r1, #0x30]
700019c0 lsls     r3, r3, #0xa
700019c2 movs     r7, #0xfc
700019c4 ldrh     r1, [r2]
700019c6 adds     r2, r2, #2
700019c8 lsls     r6, r1, #3
700019ca strb     r6, [r0]
700019cc lsrs     r6, r1, #3
700019ce ands     r6, r7
700019d0 adds     r0, r0, #1
700019d2 lsrs     r1, r1, #0xb
700019d4 strb     r6, [r0]
700019d6 adds     r0, r0, #1
700019d8 lsls     r1, r1, #3
700019da strb     r1, [r0]
700019dc adds     r0, r0, #1
700019de movs     r1, #0
700019e0 strb     r1, [r0]
700019e2 adds     r0, r0, #1
700019e4 subs     r3, r3, #1
700019e6 bne      #0x700019c4
700019e8 ldr      r0, [r4, #0x30]
700019ea blx      #0x70032e38
700019ee ldr      r0, [r5, #0x10]
700019f0 ldr      r1, [r4, #0x30]
700019f2 ldr      r2, [r0]
700019f4 adds     r1, r1, #4
700019f6 ldr      r2, [r2, #0x14]
700019f8 blx      r2
700019fa ldr      r0, [r4, #0x30]
700019fc bl       #0x70034470
70001a00 ldrb     r0, [r5, #5]
70001a02 cmp      r0, #0
70001a04 bne      #0x700019b4
70001a06 ldr      r0, [r4, #0xc]
70001a08 ldr      r1, [r4, #0x20]
70001a0a ldr      r0, [r0, #0x20]
70001a0c blx      #0x70033880

; Asphalt decompressed code, guest offset 0x1ece
70001ece push     {r4, r5, r6, r7, lr}
70001ed0 movs     r4, r0
70001ed2 sub      sp, #0x13c
70001ed4 add      r0, sp, #0x128
70001ed6 blx      #0x70033100
70001eda movs     r0, #0x29
70001edc lsls     r0, r0, #7
70001ede adds     r5, r4, r0
70001ee0 ldr      r0, [r5, #8]
70001ee2 ldr      r6, [pc, #0x1a8]
70001ee4 cmp      r0, #0
70001ee6 bne      #0x70001eee
70001ee8 ldr      r1, [pc, #0x1a4]
70001eea str      r1, [sp, #0x10c]
70001eec b        #0x70002210
70001eee movs     r0, r6
70001ef0 str      r6, [sp, #0x134]
70001ef2 blx      #0x70033048
70001ef6 movs     r0, #0x51
70001ef8 lsls     r0, r0, #6
70001efa adds     r7, r4, r0
70001efc ldr      r0, [r7, #0x38]
70001efe movs     r1, #5
70001f00 tst      r0, r1
70001f02 beq      #0x70001f0c
70001f04 movs     r0, r6
70001f06 str      r6, [sp, #0x10c]
70001f08 blx      #0x70033048
70001f0c ldr      r0, [r7, #0x38]
70001f0e cmp      r0, #8
70001f10 bne      #0x70001f1a
70001f12 movs     r0, r6

; Asphalt decompressed code, guest offset 0x21fa
700021fa add      r0, sp, #0x120
700021fc blx      #0x70033100
70002200 ldr      r2, [sp, #0x128]
70002202 ldr      r3, [sp, #0x12c]
70002204 add      r1, sp, #0x120
70002206 add      r0, sp, #0x110
70002208 blx      #0x70032f30
7000220c ldr      r1, [pc, #0x204]
7000220e str      r1, [sp, #0x134]
70002210 movs     r0, r4
70002212 blx      #0x70033110
70002216 b        #0x70001f78
