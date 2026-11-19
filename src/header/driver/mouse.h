#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "header/process/syscall.h"

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define MOUSE_BUTTON_LEFT   0x1
#define MOUSE_BUTTON_RIGHT  0x2
#define MOUSE_BUTTON_MIDDLE 0x4

// Aktifkan perangkat aux (mouse) PS/2 dan IRQ12
void mouse_init(void);

// Batas koordinat kursor, mengikuti resolusi layar grafis
void mouse_set_bounds(int32_t width, int32_t height);

// Salin posisi kursor dan status tombol terkini
void mouse_get_state(struct MouseState *state);

// Dipanggil saat IRQ12, merangkai paket 3 byte dari mouse
void mouse_isr(void);

#endif
