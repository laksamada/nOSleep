#ifndef _KERNEL_ENTRYPOINT
#define _KERNEL_ENTRYPOINT

#include "header/cpu/gdt.h"

// Optional linker variable : Pointing to kernel start & end address
// Note : Use & operator, example : a = (uint32_t) &_linker_kernel_stack_top;
extern uint32_t _linker_kernel_virtual_addr_start;
extern uint32_t _linker_kernel_virtual_addr_end;
extern uint32_t _linker_kernel_physical_addr_start;
extern uint32_t _linker_kernel_physical_addr_end;
extern uint32_t _linker_kernel_stack_top;

/**
 * Load GDT from gdtr and complete init for protected mode. This procedure implemented in asm.
 * Note: This procedure will not activate CPU interrupt flag
 * 
 * @param gdtr Pointer to already defined & initialized GDTR
 * @warning Invalid address / definition of GDT will cause bootloop after calling this procedure.
 */
extern void load_gdt(struct GDTR *gdtr);

/**
 * Set the tss register pointing to GDT_TSS_SELECTOR with ring 0
 */
extern void set_tss_register(void);  // Implemented in kernel-entrypoint.s

#endif