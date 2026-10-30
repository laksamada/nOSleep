#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/cpu/gdt.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/swap.h"
#include "header/text/framebuffer.h"

#define INTERRUPT_PAGE_FAULT 0xE

struct TSSEntry _interrupt_tss_entry = {
    .ss0  = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for stack_ptr variable
    _interrupt_tss_entry.esp0 = stack_ptr + 8; 
}


void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) {
        out(PIC2_COMMAND, PIC_ACK);
    }
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // Start initialization sequence
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    // Set vector offset
    out(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    out(PIC1_DATA, 0b0100);
    io_wait();
    // Tell Slave PIC its cascade identity (0000 0010)
    out(PIC2_DATA, 0b0010);
    io_wait();

    // Set PICs to 8086/88 mode
    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Restore saved masks
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case 0:
            *((int8_t *) frame.cpu.general.ecx) = read(
                *(struct EXT2DriverRequest *) frame.cpu.general.ebx
            );
            break;
        case 1:
            *((int8_t *) frame.cpu.general.ecx) = read_directory(
                (struct EXT2DriverRequest *) frame.cpu.general.ebx
            );
            break;
        case 2:
            *((int8_t *) frame.cpu.general.ecx) = write(
                (struct EXT2DriverRequest *) frame.cpu.general.ebx
            );
            break;
        case 3:
            *((int8_t *) frame.cpu.general.ecx) = delete(
                *(struct EXT2DriverRequest *) frame.cpu.general.ebx
            );
            break;
        case 4:
            get_keyboard_buffer((char *) frame.cpu.general.ebx);
            break;
        case 5:
            framebuffer_putchar((char) frame.cpu.general.ebx, (uint8_t) frame.cpu.general.ecx, 0);
            break;
        case 6:
            puts(
                (char *) frame.cpu.general.ebx,
                frame.cpu.general.ecx,
                frame.cpu.general.edx
            );
            break;
        case 7:
            keyboard_state_activate();
            break;
        case 8:
            framebuffer_clear();
            framebuffer_set_cursor(0, 0);
            break;
        case 9: {
            struct EXT2Inode node;
            node.i_size = 0;
            read_inode(frame.cpu.general.ebx, &node);
            *((uint32_t *) frame.cpu.general.ecx) = node.i_size;
            break;
        }
        case 10:
            *((uint32_t *) frame.cpu.general.ebx) = PAGE_FRAME_ON_SWAP_MAX_COUNT - swap_manager.free_count;
            break;
        default:
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
   switch (frame.int_number){
        case PIC1_OFFSET + IRQ_KEYBOARD: 
            keyboard_isr();
            break;
        case INTERRUPT_PAGE_FAULT:
            handle_page_fault(frame.int_stack.error_code);
            break;
        case SYSCALL_INTERRUPT_NUMBER:
            syscall(frame);
            break;
        default:
            break;
   }
}