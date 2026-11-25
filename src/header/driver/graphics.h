#ifndef _GRAPHICS_H
#define _GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include "header/process/syscall.h"

#define GRAPHICS_WIDTH         800
#define GRAPHICS_HEIGHT        600
#define GRAPHICS_BPP           32
#define GRAPHICS_WINDOW_BASE   0xE0000000u

/**
 * Layar grafis memakai Bochs VBE (BGA) milik VGA standar QEMU.
 * Gambar diletakkan mulai baris GRAPHICS_Y_OFFSET di VRAM supaya isi teks dan font
 * mode teks (di awal VRAM) tidak tertimpa, sehingga shell bisa kembali utuh.
 */
#define GRAPHICS_Y_OFFSET      128

// Cari linear framebuffer BGA lewat PCI dan petakan ke ruang kernel
void graphics_init(void);

/**
 * Layar grafis punya satu pemilik (proses yang terakhir masuk mode grafis), dan hanya pemilik
 * yang bisa menggambar. Contoh: GUI memiliki layar, lalu program grafis lain dijalankan dari
 * terminal sehingga program itu menjadi pemilik. Saat program selesai, layar kembali ke GUI. Jika pemilik terakhir selesai,
 * kernel kembali ke mode teks.
 *
 * Masuk (true) atau keluar (false) dari mode grafis, false jika BGA tidak tersedia
 */
bool graphics_set_mode(bool enable, uint32_t pid);

// Dipanggil saat proses dihapus agar layar diserahkan ke pemilik sebelumnya
void graphics_release(uint32_t pid);

bool graphics_is_owner(uint32_t pid);

// Salin font 8x16 bawaan VGA (dibaca dari VRAM plane 2 saat pertama masuk mode grafis)
void graphics_get_font(uint8_t *out);

// Salin piksel 0x00RRGGBB dari memori user ke layar, hanya untuk pemilik layar
void graphics_blit(const struct BlitRequest *request, uint32_t pid);

#endif
