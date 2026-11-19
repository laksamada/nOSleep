#include <stdio.h>
#include <nosleep.h>

#define CLOCK_ROW   24
#define CLOCK_COL   71
#define CLOCK_COLOR 0x0E

int main(void) {
    int last_second = -1;
    while (true) {
        struct CMOSTime now;
        sys_time(&now);
        if (now.second != last_second) {
            char text[16];
            snprintf(text, sizeof(text), " %02d:%02d:%02d", now.hour, now.minute, now.second);
            sys_write_at(text, CLOCK_ROW, CLOCK_COL, CLOCK_COLOR);
            last_second = now.second;
        }
        sys_sleep(100);
    }
    return 0;
}
