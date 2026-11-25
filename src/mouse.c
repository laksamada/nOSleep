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

#define VMMOUSE_MAGIC              0x564D5868u
#define VMMOUSE_PORT               0x5658
#define VMMOUSE_CMD_GETVERSION     10
#define VMMOUSE_CMD_DATA           39
#define VMMOUSE_CMD_STATUS         40
#define VMMOUSE_CMD_COMMAND        41
#define VMMOUSE_ENABLE             0x45414552u
#define VMMOUSE_REQUEST_ABSOLUTE   0x53424152u
#define VMMOUSE_VERSION_ID         0x3442554Au
#define VMMOUSE_STATUS_ERROR       0xFFFF0000u
#define VMMOUSE_PACKET_WORDS       4
#define VMMOUSE_AXIS_RANGE         0x10000
#define VMMOUSE_LEFT               0x20
#define VMMOUSE_RIGHT              0x10
#define VMMOUSE_MIDDLE             0x08

struct VmmouseRegisters {
    uint32_t eax, ebx, ecx, edx;
};

static struct {
    struct MouseState state;
    int32_t width;
    int32_t height;
    uint8_t packet[3];
    uint8_t packet_index;
    bool    absolute;
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

static void update_buttons(uint8_t buttons) {
    if ((buttons & MOUSE_BUTTON_LEFT) && !(mouse.state.buttons & MOUSE_BUTTON_LEFT))
        mouse.state.left_presses++;
    mouse.state.buttons = buttons;
}

static struct VmmouseRegisters vmmouse_call(uint32_t command, uint32_t argument) {
    struct VmmouseRegisters regs;
    __asm__ volatile("inl %%dx, %%eax"
                     : "=a"(regs.eax), "=b"(regs.ebx), "=c"(regs.ecx), "=d"(regs.edx)
                     : "a"(VMMOUSE_MAGIC), "b"(argument), "c"(command), "d"(VMMOUSE_PORT));
    return regs;
}

static bool vmmouse_init(void) {
    if (vmmouse_call(VMMOUSE_CMD_GETVERSION, 0).ebx != VMMOUSE_MAGIC)
        return false;
    vmmouse_call(VMMOUSE_CMD_COMMAND, VMMOUSE_ENABLE);
    if ((vmmouse_call(VMMOUSE_CMD_STATUS, 0).eax & 0xFFFF) == 0)
        return false;
    if (vmmouse_call(VMMOUSE_CMD_DATA, 1).eax != VMMOUSE_VERSION_ID)
        return false;
    vmmouse_call(VMMOUSE_CMD_COMMAND, VMMOUSE_REQUEST_ABSOLUTE);
    return true;
}

static void vmmouse_read_packets(void) {
    while (true) {
        uint32_t status = vmmouse_call(VMMOUSE_CMD_STATUS, 0).eax;
        if (status == VMMOUSE_STATUS_ERROR || (status & 0xFFFF) < VMMOUSE_PACKET_WORDS)
            return;

        struct VmmouseRegisters packet = vmmouse_call(VMMOUSE_CMD_DATA, VMMOUSE_PACKET_WORDS);
        mouse.state.x = (int32_t) (packet.ebx * (uint32_t) mouse.width / VMMOUSE_AXIS_RANGE);
        mouse.state.y = (int32_t) (packet.ecx * (uint32_t) mouse.height / VMMOUSE_AXIS_RANGE);
        update_buttons(((packet.eax & VMMOUSE_LEFT) ? MOUSE_BUTTON_LEFT : 0) |
                       ((packet.eax & VMMOUSE_RIGHT) ? MOUSE_BUTTON_RIGHT : 0) |
                       ((packet.eax & VMMOUSE_MIDDLE) ? MOUSE_BUTTON_MIDDLE : 0));
    }
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
    mouse.absolute = vmmouse_init();

    mouse.state.x = mouse.width / 2;
    mouse.state.y = mouse.height / 2;
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_CASCADE));
    out(PIC2_DATA, in(PIC2_DATA) & ~(1 << (IRQ_MOUSE - 8)));
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

    if (mouse.absolute) {
        vmmouse_read_packets();
        pic_ack(IRQ_MOUSE);
        return;
    }

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
        update_buttons(flags & 0x7);
        mouse.packet_index = 0;
    }
    pic_ack(IRQ_MOUSE);
}
