#include <stdint.h>
#include "header/process/syscall.h"

#define COLOR_WHITE 0x0F

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30"
        : "+a"(eax), "+b"(ebx), "+c"(ecx), "+d"(edx)
        : /* <Empty> */
        : "memory");
}

int main(void) {
    const char msg[] = "Hello World!\n";
    syscall(SYSCALL_PUTS, (uint32_t) msg, (uint32_t) (sizeof(msg) - 1), COLOR_WHITE);
    return 0;
}
