#include <stdint.h>
#include <stdbool.h>
#include "header/process/syscall.h"

#define CLOCK_ROW   24
#define CLOCK_COL   71
#define CLOCK_COLOR 0x0E
#define REFRESH_MS  100

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : "+a"(eax), "+b"(ebx), "+c"(ecx), "+d"(edx) : /* <Empty> */ : "memory");
}

static uint32_t get_ticks(void) {
    uint32_t ticks = 0;
    syscall(SYSCALL_GET_TICKS, (uint32_t) &ticks, 0, 0);
    return ticks;
}

static void sleep_ms(uint32_t ms) {
    uint32_t start = get_ticks();
    while (get_ticks() - start < ms)
        syscall(SYSCALL_YIELD, 0, 0, 0);
}

static void write_two_digits(char *text, uint8_t value) {
    text[0] = '0' + value / 10;
    text[1] = '0' + value % 10;
}

int main(void) {
    char text[] = " 00:00:00";
    int last_second = -1;

    while (true) {
        struct CMOSTime now;
        syscall(SYSCALL_GET_TIME, (uint32_t) &now, 0, 0);
        if (now.second != last_second) {
            write_two_digits(text + 1, now.hour);
            write_two_digits(text + 4, now.minute);
            write_two_digits(text + 7, now.second);
            syscall(SYSCALL_WRITE_AT, (uint32_t) text, (CLOCK_ROW << 8) | CLOCK_COL, CLOCK_COLOR);
            last_second = now.second;
        }
        sleep_ms(REFRESH_MS);
    }
    return 0;
}
