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
#include "header/driver/mouse.h"
#include "header/driver/graphics.h"
#include "header/filesystem/bootfile.h"
#include "header/stdlib/string.h"

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

static uint32_t string_length(const char *str, uint32_t max) {
    uint32_t len = 0;
    while (len < max && str[len] != '\0')
        len++;
    return len;
}

__attribute__((noreturn)) static void terminate_current_process(void) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL)
        process_destroy(current->metadata.pid);
    scheduler_switch_to_next_process();
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
    }
}

static void syscall_boot_file_read(struct BootFileRead *request, uint32_t *bytes_read) {
    const struct BootFile *file = bootfile_find(request->name, string_length(request->name, BOOTFILE_NAME_LENGTH_MAX));
    *bytes_read = 0;
    if (file == NULL || request->offset >= file->size)
        return;
    uint32_t count = file->size - request->offset;
    if (count > request->size)
        count = request->size;
    memcpy(request->buf, file->data + request->offset, count);
    *bytes_read = count;
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
            get_keyboard_buffer((char *) ebx);
            break;
        case SYSCALL_PUTCHAR:
            if (!graphics_is_active())
                framebuffer_putchar((char) ebx, (uint8_t) ecx, 0);
            break;
        case SYSCALL_PUTS:
            if (!graphics_is_active())
                puts((char *) ebx, ecx, edx);
            break;
        case SYSCALL_KEYBOARD_ON:
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
            for (uint32_t i = 0; text[i] != '\0' && col + i < 80; i++)
                framebuffer_write(row, col + i, text[i], edx & 0xF, (edx >> 4) & 0xF);
            break;
        }
        case SYSCALL_YIELD:
            scheduler_yield(frame);
        case SYSCALL_GET_TICKS:
            *((uint32_t *) ebx) = scheduler_get_ticks();
            break;
        case SYSCALL_PROCESS_ALIVE:
            *((bool *) ecx) = process_find(ebx) != NULL;
            break;
        case SYSCALL_GRAPHICS_MODE: {
            struct GraphicsInfo *info = (struct GraphicsInfo *) ecx;
            bool ok = graphics_set_mode(ebx != 0);
            info->width  = ok ? GRAPHICS_WIDTH : 0;
            info->height = ok ? GRAPHICS_HEIGHT : 0;
            break;
        }
        case SYSCALL_GRAPHICS_BLIT:
            graphics_blit((struct BlitRequest *) ebx);
            break;
        case SYSCALL_MOUSE_STATE:
            mouse_get_state((struct MouseState *) ebx);
            break;
        case SYSCALL_KEY_EVENT:
            if (!keyboard_get_event((uint16_t *) ebx))
                *((uint16_t *) ebx) = 0;
            break;
        case SYSCALL_BOOT_FILE_SIZE: {
            const char *name = (const char *) ebx;
            const struct BootFile *file = bootfile_find(name, string_length(name, BOOTFILE_NAME_LENGTH_MAX));
            *((int32_t *) ecx) = file != NULL ? (int32_t) file->size : -1;
            break;
        }
        case SYSCALL_BOOT_FILE_READ:
            syscall_boot_file_read((struct BootFileRead *) ebx, (uint32_t *) ecx);
            break;
        case SYSCALL_GRAPHICS_FONT:
            graphics_get_font((uint8_t *) ebx);
            break;
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
        case PIC1_OFFSET + IRQ_MOUSE:
            mouse_isr();
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
