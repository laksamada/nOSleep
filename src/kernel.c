#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"

// Nama fungsi utama mungkin berbeda di kode Anda, sesuaikan saja
void kernel_setup(void) {
    framebuffer_clear();
    framebuffer_write(1, 0,  'H', 0, 0x1);
    framebuffer_write(1, 1,  'a', 0, 0x2);
    framebuffer_write(1, 2, 'i', 0, 0x3);
    framebuffer_write(1, 3, '!', 0, 0x4);
    framebuffer_write(24, 79, 's', 0, 0xF);
    framebuffer_set_cursor(0, 0);
    while (true);
}
