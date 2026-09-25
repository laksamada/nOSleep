#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"
#include "header/text/framebuffer.h"

const char keyboard_scancode_1_to_ascii_map[256] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
     0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',    0,
   '*',   0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0, '-',   0,   0,
     0, '+',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on  = false,
    .keyboard_buffer    = 0
};

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    if (buf != NULL) {
        *buf = keyboard_state.keyboard_buffer;
        keyboard_state.keyboard_buffer = 0;
    }
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);
    uint8_t row, col;

    framebuffer_get_cursor(&row, &col);

    if (!keyboard_state.keyboard_input_on) {
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
    } else {
        if (keyboard_state.read_extended_mode) {
            switch (scancode) {
                case EXT_SCANCODE_UP:
                if(row>0){
                    framebuffer_set_cursor(row - 1, col);
                    }
                    break;
                case EXT_SCANCODE_DOWN:
                if(row<24){
                framebuffer_set_cursor(row + 1, col);
                    }
                    break;
                case EXT_SCANCODE_LEFT:
                if(col>0){
                framebuffer_set_cursor(row, col - 1);
                    }
                    break;
                case EXT_SCANCODE_RIGHT:
                if(col<80){
                framebuffer_set_cursor(row, col + 1);
                    }
                    break;
                default:
                    break;
            }
            keyboard_state.read_extended_mode = false;
        } else {
            if (!(scancode & 0x80)) {
                char ascii = keyboard_scancode_1_to_ascii_map[scancode];
                if (ascii != 0) {
                    keyboard_state.keyboard_buffer = ascii;
                }
            }
        }
    }

    pic_ack(IRQ_KEYBOARD);
}