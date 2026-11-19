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
    .keyboard_buffer    = 0,
    .shift_pressed      = false
};

static char apply_shift(char ascii) {
    if (ascii >= 'a' && ascii <= 'z')
        return ascii - 'a' + 'A';

    switch (ascii) {
        case '1':  return '!';
        case '2':  return '@';
        case '3':  return '#';
        case '4':  return '$';
        case '5':  return '%';
        case '6':  return '^';
        case '7':  return '&';
        case '8':  return '*';
        case '9':  return '(';
        case '0':  return ')';
        case '-':  return '_';
        case '=':  return '+';
        case '[':  return '{';
        case ']':  return '}';
        case ';':  return ':';
        case '\'': return '"';
        case '`':  return '~';
        case '\\': return '|';
        case ',':  return '<';
        case '.':  return '>';
        case '/':  return '?';
        default:   return ascii;
    }
}

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

static struct {
    uint16_t events[KEYBOARD_EVENT_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    bool     extended;
} key_events;

static void record_key_event(uint8_t scancode) {
    if (scancode == EXTENDED_SCANCODE_BYTE) {
        key_events.extended = true;
        return;
    }

    uint16_t event = scancode & ~SCANCODE_RELEASE_BIT;
    if (scancode & SCANCODE_RELEASE_BIT)
        event |= KEY_EVENT_RELEASED;
    if (key_events.extended)
        event |= KEY_EVENT_EXTENDED;
    key_events.extended = false;

    uint32_t next = (key_events.tail + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    if (next != key_events.head) {
        key_events.events[key_events.tail] = event;
        key_events.tail = next;
    }
}

bool keyboard_get_event(uint16_t *event) {
    if (key_events.head == key_events.tail)
        return false;
    *event = key_events.events[key_events.head];
    key_events.head = (key_events.head + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    return true;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);
    uint8_t row, col;

    record_key_event(scancode);

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
                if(row<FRAMEBUFFER_TEXT_ROWS-1){
                framebuffer_set_cursor(row + 1, col);
                    }
                    break;
                case EXT_SCANCODE_LEFT:
                if(col>0){
                framebuffer_set_cursor(row, col - 1);
                    }
                    break;
                case EXT_SCANCODE_RIGHT:
                if(col<79){
                framebuffer_set_cursor(row, col + 1);
                    }
                    break;
                default:
                    break;
            }
            keyboard_state.read_extended_mode = false;
        } else {
            uint8_t key = scancode & ~SCANCODE_RELEASE_BIT;
            if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
                keyboard_state.shift_pressed = !(scancode & SCANCODE_RELEASE_BIT);
            } else if (!(scancode & SCANCODE_RELEASE_BIT)) {
                char ascii = keyboard_scancode_1_to_ascii_map[scancode];
                if (ascii != 0) {
                    keyboard_state.keyboard_buffer = keyboard_state.shift_pressed ? apply_shift(ascii) : ascii;
                }
            }
        }
    }

    pic_ack(IRQ_KEYBOARD);
}