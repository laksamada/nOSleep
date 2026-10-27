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

static void print_line(uint8_t row, const char *label, bool ok) {
    uint8_t col = 0;
    for (const char *p = label; *p; p++)
        framebuffer_write(row, col++, *p, 0x0F, 0x00);
    const char *res = ok ? " PASS" : " FAIL";
    uint8_t fg = ok ? 0x0A : 0x0C;
    for (const char *p = res; *p; p++)
        framebuffer_write(row, col++, *p, fg, 0x00);
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    struct PageDirectory *pd = paging_get_current_page_directory_addr();
    print_line(0, "T1 PD aktif == kernel PD", pd == &_paging_kernel_page_directory);

    uint32_t free0 = page_manager_state.free_page_frame_count;
    void *va = (void *) 0x400000;           // user space, PDE index 1 (belum ada page table)
    volatile uint32_t *p = (volatile uint32_t *) va;

    print_line(1, "T2 alokasi 0x400000", paging_allocate_user_page_frame(pd, va));
    print_line(2, "T3 frame bebas turun 2 (page + PT)",
               free0 - page_manager_state.free_page_frame_count == 2);

    *p = 0xDEADBEEF;
    print_line(3, "T4 tulis/baca memori", *p == 0xDEADBEEF);

    print_line(4, "T5 alokasi ganda ditolak", !paging_allocate_user_page_frame(pd, va));
    print_line(5, "T6 alamat kernel ditolak",
               !paging_allocate_user_page_frame(pd, (void *) 0xC0000000));

    print_line(6, "T7 free", paging_free_user_page_frame(pd, va));
    print_line(7, "T8 frame bebas kembali (selisih 1)",
               free0 - page_manager_state.free_page_frame_count == 1);
    print_line(8, "T9 free ganda ditolak", !paging_free_user_page_frame(pd, va));

    // Frame lama pernah berisi 0xDEADBEEF; harus sudah dibersihkan saat dialokasi ulang
    bool realloc_ok = paging_allocate_user_page_frame(pd, va);
    print_line(9, "T10 alokasi ulang & terisi nol", realloc_ok && *p == 0);

    while (1);
}
