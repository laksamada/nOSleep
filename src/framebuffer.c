#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    // 1. Hitung posisi linear (1D) dari baris dan kolom. 
    // Lebar layar adalah 80 karakter.
    uint16_t pos = r * 80 + c;

    // 2. Kirim byte rendah (low byte) dari posisi kursor
    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t)(pos & 0xFF));

    // 3. Kirim byte tinggi (high byte) dari posisi kursor
    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint16_t index = (row * 80 + col) * 2;

    // 2. Indeks genap (index): Masukkan karakter ASCII
    FRAMEBUFFER_MEMORY_OFFSET[index] = c;

    // 3. Indeks ganjil (index + 1): Masukkan kode warna.
    // Background digeser 4 bit ke kiri, lalu digabung (OR) dengan foreground.
    FRAMEBUFFER_MEMORY_OFFSET[index + 1] = (bg << 4) | (fg & 0x0F);
}

void framebuffer_clear(void) {
    for (uint8_t row = 0; row < 25; row++) {
        for (uint8_t col = 0; col < 80; col++) {
            // Mengubah seluruh sel menjadi karakter kosong (0x00)
            // Foreground: Abu-abu (0x07), Background: Hitam (0x00)
            framebuffer_write(row, col, 0x00, 0x07, 0x00);
        }
    }
}

void framebuffer_putchar(char c, uint8_t fg, uint8_t bg) {
    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = 79;
        }
        framebuffer_write(cursor_row, cursor_col, ' ', fg, bg);
    } else {
        framebuffer_write(cursor_row, cursor_col, c, fg, bg);
        cursor_col++;
        if (cursor_col >= 80) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    // Scroll sederhana jika melebihi baris 24
    if (cursor_row >= 25) {
        framebuffer_clear();
        cursor_row = 0;
        cursor_col = 0;
    }

    framebuffer_set_cursor(cursor_row, cursor_col);
}