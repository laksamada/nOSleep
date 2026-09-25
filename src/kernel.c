#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/keyboard.h"


// Nama fungsi utama mungkin berbeda di kode Anda, sesuaikan saja
void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    keyboard_state_activate();
    framebuffer_clear();
    framebuffer_write(1, 0,  'H', 0, 0x1);
    framebuffer_write(1, 1,  'a', 0, 0x2);
    framebuffer_write(1, 2, 'i', 0, 0x3);
    framebuffer_write(1, 3, '!', 0, 0x4);
    framebuffer_write(24, 79, 's', 0, 0xF);
    framebuffer_set_cursor(0, 0);
    __asm__("int $0x4");
    __asm__ volatile("sti");

    while (true) {
        char buf = 0;
        get_keyboard_buffer(&buf);
        if (buf != 0) {
            framebuffer_putchar(buf, 0x07, 0x00);
        }
        __asm__ volatile("hlt");
    }
}
