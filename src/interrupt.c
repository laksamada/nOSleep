#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/gdt.h"
#include "header/cpu/idt.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/swap.h"
#include "header/text/framebuffer.h"
#include "header/scheduler/scheduler.h"
#include "header/process/syscall.h"
#include "header/driver/cmos.h"

#define INTERRUPT_PAGE_FAULT 0xE
#define INIT_PROCESS_PID     1

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile("mov %%ebp, %0" : "=r"(stack_ptr) : /* <Empty> */);
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

static struct {
    uint32_t owner_pid;
    uint8_t  col;
    uint8_t  len;
} status_row_owner;

static void clear_status_row_if_owned(uint32_t pid) {
    if (status_row_owner.owner_pid != 0 && status_row_owner.owner_pid == pid) {
        for (uint8_t i = 0; i < status_row_owner.len && status_row_owner.col + i < 80; i++)
            framebuffer_write(FRAMEBUFFER_STATUS_ROW, status_row_owner.col + i, ' ', 0x07, 0x00);
        status_row_owner.owner_pid = 0;
        status_row_owner.col = 0;
        status_row_owner.len = 0;
    }
}

__attribute__((noreturn)) static void terminate_current_process(void) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL) {
        uint32_t pid = current->metadata.pid;
        clear_status_row_if_owned(pid);
        process_destroy(pid);
    }
    scheduler_switch_to_next_process();
}

static void mark_current_uses_keyboard(void) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL)
        current->metadata.uses_keyboard = true;
}

static void syscall_kill(uint32_t pid, int32_t *result) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (pid == INIT_PROCESS_PID) {
        *result = 2;
    } else if (process_find(pid) == NULL) {
        *result = 1;
    } else {
        *result = 0;
        if (current != NULL && current->metadata.pid == pid)
            terminate_current_process();
        process_destroy(pid);
        clear_status_row_if_owned(pid);
    }
}

void syscall(struct InterruptFrame frame) {
    uint32_t ebx = frame.cpu.general.ebx;
    uint32_t ecx = frame.cpu.general.ecx;
    uint32_t edx = frame.cpu.general.edx;

    switch (frame.cpu.general.eax) {
        case SYSCALL_READ:
            *((int8_t *) ecx) = read(*(struct EXT2DriverRequest *) ebx);
            break;
        case SYSCALL_READ_DIRECTORY:
            *((int8_t *) ecx) = read_directory((struct EXT2DriverRequest *) ebx);
            break;
        case SYSCALL_WRITE:
            *((int8_t *) ecx) = write((struct EXT2DriverRequest *) ebx);
            break;
        case SYSCALL_DELETE:
            *((int8_t *) ecx) = delete(*(struct EXT2DriverRequest *) ebx);
            break;
        case SYSCALL_GETCHAR:
            mark_current_uses_keyboard();
            get_keyboard_buffer((char *) ebx);
            break;
        case SYSCALL_PUTCHAR:
            framebuffer_putchar((char) ebx, (uint8_t) ecx, 0);
            break;
        case SYSCALL_PUTS:
            puts((char *) ebx, ecx, edx);
            break;
        case SYSCALL_KEYBOARD_ON:
            mark_current_uses_keyboard();
            keyboard_state_activate();
            break;
        case SYSCALL_CLEAR_SCREEN:
            framebuffer_clear();
            framebuffer_set_cursor(0, 0);
            break;
        case SYSCALL_FILE_SIZE: {
            struct EXT2Inode node;
            node.i_size = 0;
            read_inode(ebx, &node);
            *((uint32_t *) ecx) = node.i_size;
            break;
        }
        case SYSCALL_SWAP_USED:
            *((uint32_t *) ebx) = PAGE_FRAME_ON_SWAP_MAX_COUNT - swap_manager.free_count;
            break;
        case SYSCALL_EXEC: {
            uint32_t pid = 0;
            *((int32_t *) ecx) = process_create_user_process(*(struct EXT2DriverRequest *) ebx, &pid);
            *((uint32_t *) edx) = pid;
            break;
        }
        case SYSCALL_PS:
            *((uint32_t *) edx) = process_list_info((struct ProcessInfo *) ebx, ecx);
            break;
        case SYSCALL_KILL:
            syscall_kill(ebx, (int32_t *) ecx);
            break;
        case SYSCALL_EXIT:
            terminate_current_process();
        case SYSCALL_GET_TIME:
            cmos_get_time((struct CMOSTime *) ebx);
            break;
        case SYSCALL_WRITE_AT: {
            const char *text = (const char *) ebx;
            uint8_t row = ecx >> 8, col = ecx & 0xFF;
            if (row >= 25 || col >= 80)
                break;
            uint32_t written = 0;
            for (uint32_t i = 0; text[i] != '\0' && col + i < 80; i++) {
                framebuffer_write(row, col + i, text[i], edx & 0xF, (edx >> 4) & 0xF);
                written++;
            }
            if (row == FRAMEBUFFER_STATUS_ROW && written > 0) {
                struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
                if (current != NULL) {
                    status_row_owner.owner_pid = current->metadata.pid;
                    status_row_owner.col = col;
                    status_row_owner.len = (uint8_t) written;
                }
            }
            break;
        }
        case SYSCALL_YIELD:
            scheduler_yield(frame);
        case SYSCALL_GET_TICKS:
            *((uint32_t *) ebx) = scheduler_get_ticks();
            break;
        case SYSCALL_PROCESS_ALIVE: {
            struct ProcessControlBlock *pcb = process_find(ebx);
            *((bool *) ecx) = pcb != NULL;
            if (edx != 0)
                *((bool *) edx) = pcb != NULL && pcb->metadata.uses_keyboard;
            break;
        }
        default:
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    bool from_user = (frame.int_stack.cs & 0x3) == 0x3;

    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            scheduler_handle_timer(frame);
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case INTERRUPT_PAGE_FAULT:
            if (!handle_page_fault(frame.int_stack.error_code))
                terminate_current_process();
            break;
        case SYSCALL_INTERRUPT_NUMBER:
            syscall(frame);
            break;
        default:
            if (frame.int_number < PIC1_OFFSET && from_user) {
                puts("\nprogram killed by CPU exception\n", 33, 0xC);
                terminate_current_process();
            }
            break;
    }
}
