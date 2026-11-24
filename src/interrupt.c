#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/cpu/gdt.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/swap.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/scheduler/scheduler.h"
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
        case 11: { // exit
            // ebx: exit code (opsional jika disimpan/dicetak)
            struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
            if (current_pcb != NULL) {
                process_destroy(current_pcb->metadata.pid);
            }
            // Panggil scheduler agar langsung pindah ke proses lain (tidak return ke proses mati)
            scheduler_switch_to_next_process();
            break;
        }
        case 12: // create process (exec)
            // ebx: struct EXT2DriverRequest* request
            // ecx: int32_t* return_code (PID baru atau kode error)
            *((int32_t *) frame.cpu.general.ecx) = process_create_user_process(
                *(struct EXT2DriverRequest *) frame.cpu.general.ebx
            );
            break;
        case 13: // terminate by PID (kill)
            // ebx: uint32_t pid
            // ecx: bool* return_code (true jika berhasil, false jika PID tidak ditemukan)
            {
                struct ProcessControlBlock *current_pcb = process_get_current_running_pcb_pointer();
                uint32_t current_pid = current_pcb == NULL ? 0 : current_pcb->metadata.pid;
                bool result = process_destroy(frame.cpu.general.ebx);
                *((bool *) frame.cpu.general.ecx) = result;
                if (result && frame.cpu.general.ebx == current_pid)
                    scheduler_switch_to_next_process();
            }
            break;
        case 14: // get process info (ps)
            // ebx: struct ProcessInfo* buffer (atau PCB metadata buffer)
            // ecx: uint32_t* count (jumlah proses aktif yang disalin)
            process_get_all_info(
                (struct ProcessInfo *) frame.cpu.general.ebx,
                (uint32_t *) frame.cpu.general.ecx
            );
            break;
        case 15: // get CMOS time (clock)
            // ebx: struct CMOSTime* time_buffer
            cmos_get_time(
                (struct CMOSTime *) frame.cpu.general.ebx
            );
            break;    
        default:
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
   switch (frame.int_number){
        case PIC1_OFFSET + IRQ_TIMER:
            pic_ack(IRQ_TIMER);
            if ((frame.int_stack.cs & 0x3) == 0x3 &&
                process_get_current_running_pcb_pointer() != NULL) {
                struct Context context = {
                    .cpu = frame.cpu,
                    .eip = frame.int_stack.eip,
                    .eflags = frame.int_stack.eflags,
                    .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
                };
                scheduler_save_context_to_current_running_pcb(context);
                scheduler_switch_to_next_process();
            }
            break;
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