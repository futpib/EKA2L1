.section .multiboot,"a"
.balign 4
.long 0x1badb002
.long 0
.long -(0x1badb002)
.section .text
.global _start
_start:
 cli
 mov $stack_top,%esp
 call main_guest
1: hlt
 jmp 1b
.section .bss
.balign 16
.skip 65536
stack_top:
