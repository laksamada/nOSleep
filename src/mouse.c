#include "header/driver/mouse.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"

#define PS2_OUTPUT_FULL       0x01
#define PS2_INPUT_FULL        0x02
#define PS2_AUX_DATA          0x20
#define PS2_ENABLE_AUX        0xA8
#define PS2_READ_CONFIG       0x20
#define PS2_WRITE_CONFIG      0x60
#define PS2_WRITE_AUX         0xD4
#define PS2_CONFIG_AUX_IRQ    0x02
#define PS2_CONFIG_AUX_CLOCK  0x20
#define MOUSE_SET_DEFAULTS    0xF6
#define MOUSE_ENABLE_STREAM   0xF4
#define MOUSE_PACKET_ALIGNED  0x08
#define MOUSE_X_SIGN          0x10
#define MOUSE_Y_SIGN          0x20
#define PS2_TIMEOUT           100000

static struct {
    struct MouseState state;
    int32_t width;
    int32_t height;
    uint8_t packet[3];
    uint8_t packet_index;
} mouse = {
    .width  = 800,
    .height = 600,
};

static void ps2_wait_write(void) {
    for (int i = 0; i < PS2_TIMEOUT && (in(PS2_STATUS_PORT) & PS2_INPUT_FULL); i++)
        ;
}

static void ps2_wait_read(void) {
    for (int i = 0; i < PS2_TIMEOUT && !(in(PS2_STATUS_PORT) & PS2_OUTPUT_FULL); i++)
        ;
}

static void mouse_write(uint8_t value) {
    ps2_wait_write();
    out(PS2_COMMAND_PORT, PS2_WRITE_AUX);
    ps2_wait_write();
    out(PS2_DATA_PORT, value);
    ps2_wait_read();
    in(PS2_DATA_PORT);
}

static int32_t clamp(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : (value > high ? high : value);
}

void mouse_init(void) {
    ps2_wait_write();
    out(PS2_COMMAND_PORT, PS2_ENABLE_AUX);

    ps2_wait_write();
    out(PS2_COMMAND_PORT, PS2_READ_CONFIG);
    ps2_wait_read();
    uint8_t config = (in(PS2_DATA_PORT) | PS2_CONFIG_AUX_IRQ) & ~PS2_CONFIG_AUX_CLOCK;
    ps2_wait_write();
    out(PS2_COMMAND_PORT, PS2_WRITE_CONFIG);
    ps2_wait_write();
    out(PS2_DATA_PORT, config);

    mouse_write(MOUSE_SET_DEFAULTS);
    mouse_write(MOUSE_ENABLE_STREAM);

    mouse.state.x = mouse.width / 2;
    mouse.state.y = mouse.height / 2;
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_CASCADE));
    out(PIC2_DATA, in(PIC2_DATA) & ~(1 << (IRQ_MOUSE - 8)));
}

void mouse_set_bounds(int32_t width, int32_t height) {
    mouse.width   = width;
    mouse.height  = height;
    mouse.state.x = clamp(mouse.state.x, 0, width - 1);
    mouse.state.y = clamp(mouse.state.y, 0, height - 1);
}

void mouse_get_state(struct MouseState *state) {
    *state = mouse.state;
}

void mouse_isr(void) {
    uint8_t status = in(PS2_STATUS_PORT);
    if (!(status & PS2_OUTPUT_FULL) || !(status & PS2_AUX_DATA)) {
        pic_ack(IRQ_MOUSE);
        return;
    }
    uint8_t data = in(PS2_DATA_PORT);

    if (mouse.packet_index == 0 && !(data & MOUSE_PACKET_ALIGNED)) {
        pic_ack(IRQ_MOUSE);
        return;
    }
    mouse.packet[mouse.packet_index++] = data;

    if (mouse.packet_index == 3) {
        uint8_t flags = mouse.packet[0];
        int32_t dx = mouse.packet[1] - ((flags & MOUSE_X_SIGN) ? 256 : 0);
        int32_t dy = mouse.packet[2] - ((flags & MOUSE_Y_SIGN) ? 256 : 0);
        mouse.state.x = clamp(mouse.state.x + dx, 0, mouse.width - 1);
        mouse.state.y = clamp(mouse.state.y - dy, 0, mouse.height - 1);
        mouse.state.buttons = flags & 0x7;
        mouse.packet_index = 0;
    }
    pic_ack(IRQ_MOUSE);
}
