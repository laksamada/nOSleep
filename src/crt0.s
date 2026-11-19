global _start
extern main

SYSCALL_EXIT equ 11

section .text
_start:
    call main
    mov  ebx, eax
    mov  eax, SYSCALL_EXIT
    int  0x30
    jmp  $
