#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nosleep.h>

#include "doomkeys.h"
#include "doomgeneric.h"

#define SCANCODE_ESCAPE    0x01
#define SCANCODE_MINUS     0x0C
#define SCANCODE_EQUALS    0x0D
#define SCANCODE_BACKSPACE 0x0E
#define SCANCODE_TAB       0x0F
#define SCANCODE_ENTER     0x1C
#define SCANCODE_CTRL      0x1D
#define SCANCODE_LSHIFT    0x2A
#define SCANCODE_RSHIFT    0x36
#define SCANCODE_ALT       0x38
#define SCANCODE_SPACE     0x39
#define SCANCODE_F1        0x3B
#define SCANCODE_F10       0x44
#define SCANCODE_UP        0x48
#define SCANCODE_LEFT      0x4B
#define SCANCODE_RIGHT     0x4D
#define SCANCODE_DOWN      0x50
#define SCANCODE_F11       0x57
#define SCANCODE_F12       0x58

static const char scancode_to_ascii[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0,   0,   0,    0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,   'a',  's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,  '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/',
};

static int32_t screen_x, screen_y;

static unsigned char doom_key(uint16_t event) {
    uint8_t scancode = event & 0x7F;
    switch (scancode) {
        case SCANCODE_UP:        return KEY_UPARROW;
        case SCANCODE_DOWN:      return KEY_DOWNARROW;
        case SCANCODE_LEFT:      return KEY_LEFTARROW;
        case SCANCODE_RIGHT:     return KEY_RIGHTARROW;
        case SCANCODE_CTRL:      return KEY_FIRE;
        case SCANCODE_SPACE:     return KEY_USE;
        case SCANCODE_ENTER:     return KEY_ENTER;
        case SCANCODE_ESCAPE:    return KEY_ESCAPE;
        case SCANCODE_LSHIFT:
        case SCANCODE_RSHIFT:    return KEY_RSHIFT;
        case SCANCODE_ALT:       return KEY_RALT;
        case SCANCODE_TAB:       return KEY_TAB;
        case SCANCODE_BACKSPACE: return KEY_BACKSPACE;
        case SCANCODE_MINUS:     return KEY_MINUS;
        case SCANCODE_EQUALS:    return KEY_EQUALS;
        case SCANCODE_F11:       return KEY_F11;
        case SCANCODE_F12:       return KEY_F12;
        default:
            break;
    }
    if (scancode >= SCANCODE_F1 && scancode <= SCANCODE_F10)
        return KEY_F1 + (scancode - SCANCODE_F1);
    if (scancode < sizeof(scancode_to_ascii))
        return scancode_to_ascii[scancode];
    return 0;
}

void DG_Init(void) {
    struct GraphicsInfo info;
    if (!sys_graphics_mode(true, &info)) {
        printf("doom: graphics mode not available\n");
        exit(1);
    }
    screen_x = (info.width - DOOMGENERIC_RESX) / 2;
    screen_y = (info.height - DOOMGENERIC_RESY) / 2;

    static uint32_t black_line[800];
    for (uint32_t y = 0; y < info.height; y++)
        sys_blit(black_line, 0, y, info.width, 1);
    while (sys_key_event() != 0)
        ;
}

void DG_DrawFrame(void) {
    sys_blit(DG_ScreenBuffer, screen_x, screen_y, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_SleepMs(uint32_t ms) {
    sys_sleep(ms);
}

uint32_t DG_GetTicksMs(void) {
    return sys_ticks();
}

int DG_GetKey(int *pressed, unsigned char *key) {
    uint16_t event;
    while ((event = sys_key_event()) != 0) {
        unsigned char doom = doom_key(event);
        if (doom != 0) {
            *pressed = !(event & KEY_EVENT_RELEASED);
            *key = doom;
            return 1;
        }
    }
    return 0;
}

void DG_SetWindowTitle(const char *title) {
    (void) title;
}

int main(void) {
    char *argv[] = {"doom", "-iwad", "doom1.wad", NULL};
    doomgeneric_Create(3, argv);
    while (true)
        doomgeneric_Tick();
    return 0;
}
