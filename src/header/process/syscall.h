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

#endif
