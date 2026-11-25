global process_context_switch

; Offset field struct Context (header/process/process.h)
CONTEXT_EDI    equ 0
CONTEXT_ESI    equ 4
CONTEXT_EBP    equ 8
CONTEXT_ESP    equ 12
CONTEXT_EBX    equ 16
CONTEXT_EDX    equ 20
CONTEXT_ECX    equ 24
CONTEXT_EAX    equ 28
CONTEXT_GS     equ 32
CONTEXT_FS     equ 36
CONTEXT_ES     equ 40
CONTEXT_DS     equ 44
CONTEXT_EIP    equ 48
CONTEXT_EFLAGS equ 52

USER_CODE_SELECTOR equ 0x18 | 0x3

section .text
; void process_context_switch(struct Context ctx)
; Context dikirim by value, jadi isinya ada di [esp + 4]
process_context_switch:
    lea  ecx, [esp + 4]

    ; Frame iret untuk pindah ke ring 3: ss, esp, eflags, cs, eip
    push dword [ecx + CONTEXT_DS]
    push dword [ecx + CONTEXT_ESP]
    push dword [ecx + CONTEXT_EFLAGS]
    push dword USER_CODE_SELECTOR
    push dword [ecx + CONTEXT_EIP]

    mov  eax, [ecx + CONTEXT_GS]
    mov  gs, ax
    mov  eax, [ecx + CONTEXT_FS]
    mov  fs, ax
    mov  eax, [ecx + CONTEXT_ES]
    mov  es, ax
    mov  eax, [ecx + CONTEXT_DS]
    mov  ds, ax

    mov  edi, [ecx + CONTEXT_EDI]
    mov  esi, [ecx + CONTEXT_ESI]
    mov  ebp, [ecx + CONTEXT_EBP]
    mov  ebx, [ecx + CONTEXT_EBX]
    mov  edx, [ecx + CONTEXT_EDX]
    mov  eax, [ecx + CONTEXT_EAX]
    mov  ecx, [ecx + CONTEXT_ECX]
    iret
