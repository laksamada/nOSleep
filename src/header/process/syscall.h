#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>

#define SYSCALL_READ              0
#define SYSCALL_READ_DIRECTORY    1
#define SYSCALL_WRITE             2
#define SYSCALL_DELETE            3
#define SYSCALL_GETCHAR           4
#define SYSCALL_PUTCHAR           5
#define SYSCALL_PUTS              6
#define SYSCALL_KEYBOARD_ON       7
#define SYSCALL_CLEAR_SCREEN      8
#define SYSCALL_FILE_SIZE         9
#define SYSCALL_SWAP_USED         10
#define SYSCALL_EXIT              11
#define SYSCALL_EXEC              12
#define SYSCALL_KILL              13
#define SYSCALL_PS                14
#define SYSCALL_GET_TIME          15
#define SYSCALL_WRITE_AT          16
#define SYSCALL_YIELD             17
#define SYSCALL_GET_TICKS         18
#define SYSCALL_PROCESS_ALIVE     19
#define SYSCALL_GRAPHICS_MODE     20
#define SYSCALL_GRAPHICS_BLIT     21
#define SYSCALL_MOUSE_STATE       22
#define SYSCALL_KEY_EVENT         23
#define SYSCALL_BOOT_FILE_SIZE    24
#define SYSCALL_BOOT_FILE_READ    25
#define SYSCALL_GRAPHICS_FONT     26

#define KEY_EVENT_RELEASED 0x8000
#define KEY_EVENT_EXTENDED 0x0100

#define GRAPHICS_FONT_HEIGHT 16
#define GRAPHICS_FONT_SIZE   (256 * GRAPHICS_FONT_HEIGHT)

struct ProcessInfo {
    uint32_t pid;
    char     name[32];
    uint32_t state;
    uint32_t frame_count;
};

struct CMOSTime {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
};

struct GraphicsInfo {
    uint32_t width;
    uint32_t height;
};

struct BlitRequest {
    const uint32_t *pixels;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
};

struct MouseState {
    int32_t x;
    int32_t y;
    uint8_t buttons;
};

struct BootFileRead {
    const char *name;
    uint32_t    offset;
    void       *buf;
    uint32_t    size;
};

#endif
