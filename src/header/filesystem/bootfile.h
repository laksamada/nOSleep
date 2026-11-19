#ifndef _BOOTFILE_H
#define _BOOTFILE_H

#include <stdint.h>
#include <stdbool.h>

#define BOOTFILE_COUNT_MAX       8
#define BOOTFILE_NAME_LENGTH_MAX 32
#define BOOTFILE_WINDOW_BASE     0xD0000000u

/**
 * File boot adalah modul yang dimuat GRUB dari ISO (baris "module" di menu.lst).
 * Dipakai untuk file besar seperti doom dan doom1.wad yang tidak muat di disk ext2 4 MiB.
 * Isinya read-only dan dipetakan ke ruang kernel mulai BOOTFILE_WINDOW_BASE.
 */
struct BootFile {
    char           name[BOOTFILE_NAME_LENGTH_MAX];
    const uint8_t *data;
    uint32_t       size;
};

// Baca daftar modul dari multiboot info (alamat fisik dari register ebx saat boot)
void bootfile_init(uint32_t multiboot_info_physical_addr);

// Cari file boot berdasarkan nama, NULL jika tidak ada
const struct BootFile* bootfile_find(const char *name, uint32_t name_len);

#endif
