global process_context_switch

KERNEL_VIRTUAL_ADDRESS_BASE equ 0xC0000000
USER_CODE_SEGMENT_SELECTOR  equ 0x1B
USER_DATA_SEGMENT_SELECTOR  equ 0x23

section .text
process_context_switch:
    mov eax, [esp + 4]
    mov edx, [eax + 56]
    sub edx, KERNEL_VIRTUAL_ADDRESS_BASE
    mov cr3, edx

    mov edi, eax
    push dword [edi + 44]
    push dword [edi + 12]
    push dword [edi + 52]
    push dword USER_CODE_SEGMENT_SELECTOR
    push dword [edi + 48]

    mov ax, USER_DATA_SEGMENT_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, [edi + 8]
    mov ebx, [edi + 16]
    mov edx, [edi + 20]
    mov ecx, [edi + 24]
    mov eax, [edi + 28]
    mov esi, [edi + 4]
    mov edi, [edi]
    iret
