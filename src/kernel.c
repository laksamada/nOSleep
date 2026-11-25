#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/process/process.h"
#include "header/scheduler/scheduler.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    initialize_filesystem_ext2();
    gdt_install_tss();
    set_tss_register();
    activate_keyboard_interrupt();
    initialize_idt();

    struct EXT2DriverRequest request = {
        .buf          = (uint8_t *) 0,
        .name         = "shell",
        .parent_inode = 1,
        .buffer_size  = PROCESS_IMAGE_SIZE_MAX,
        .name_len     = 5,
    };
    uint32_t pid;
    if (process_create_user_process(request, &pid) != PROCESS_CREATE_SUCCESS) {
        puts("shell not found, run make disk and make insert-shell\n", 53, 0xC);
        while (true);
    }

    set_tss_kernel_current_stack();
    scheduler_init();
    scheduler_switch_to_next_process();
}
