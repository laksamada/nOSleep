#include "header/driver/graphics.h"
#include "header/cpu/portio.h"
#include "header/memory/paging.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define BGA_PCI_ID         0x11111234u

#define BGA_INDEX_PORT     0x01CE
#define BGA_DATA_PORT      0x01CF
#define BGA_INDEX_XRES        1
#define BGA_INDEX_YRES        2
#define BGA_INDEX_BPP         3
#define BGA_INDEX_ENABLE      4
#define BGA_INDEX_VIRT_WIDTH  6
#define BGA_INDEX_VIRT_HEIGHT 7
#define BGA_INDEX_Y_OFFSET    9
#define BGA_ENABLED        0x01
#define BGA_LFB_ENABLED    0x40
#define BGA_NO_CLEAR_MEM   0x80

#define VGA_MISC_READ      0x3CC
#define VGA_MISC_WRITE     0x3C2
#define VGA_SEQ_INDEX      0x3C4
#define VGA_SEQ_DATA       0x3C5
#define VGA_CRTC_INDEX     0x3D4
#define VGA_CRTC_DATA      0x3D5
#define VGA_GC_INDEX       0x3CE
#define VGA_GC_DATA        0x3CF
#define VGA_AC_INDEX       0x3C0
#define VGA_AC_READ        0x3C1
#define VGA_INPUT_STATUS   0x3DA
#define VGA_AC_ENABLE      0x20
#define VGA_CRTC_PROTECT   0x11
#define VGA_FONT_GLYPH_STRIDE 32
#define VGA_PLANE_WINDOW   ((const volatile uint8_t *) 0xC00A0000)

static uint32_t *framebuffer;
static bool active;
static uint8_t font[GRAPHICS_FONT_SIZE];
static bool font_loaded;

static struct {
    uint8_t misc;
    uint8_t sequencer[5];
    uint8_t crtc[25];
    uint8_t graphics[9];
    uint8_t attribute[21];
} saved_vga;

static uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t offset) {
    out32(PCI_CONFIG_ADDRESS, 0x80000000u | (bus << 16) | (device << 11) | (offset & 0xFC));
    return in32(PCI_CONFIG_DATA);
}

static void bga_write(uint16_t index, uint16_t value) {
    out16(BGA_INDEX_PORT, index);
    out16(BGA_DATA_PORT, value);
}

static void vga_save_registers(void) {
    saved_vga.misc = in(VGA_MISC_READ);
    for (uint8_t i = 0; i < sizeof(saved_vga.sequencer); i++) {
        out(VGA_SEQ_INDEX, i);
        saved_vga.sequencer[i] = in(VGA_SEQ_DATA);
    }
    for (uint8_t i = 0; i < sizeof(saved_vga.crtc); i++) {
        out(VGA_CRTC_INDEX, i);
        saved_vga.crtc[i] = in(VGA_CRTC_DATA);
    }
    for (uint8_t i = 0; i < sizeof(saved_vga.graphics); i++) {
        out(VGA_GC_INDEX, i);
        saved_vga.graphics[i] = in(VGA_GC_DATA);
    }
    for (uint8_t i = 0; i < sizeof(saved_vga.attribute); i++) {
        in(VGA_INPUT_STATUS);
        out(VGA_AC_INDEX, i);
        saved_vga.attribute[i] = in(VGA_AC_READ);
    }
    in(VGA_INPUT_STATUS);
    out(VGA_AC_INDEX, VGA_AC_ENABLE);
}

static void vga_restore_registers(void) {
    out(VGA_MISC_WRITE, saved_vga.misc);
    for (uint8_t i = 0; i < sizeof(saved_vga.sequencer); i++) {
        out(VGA_SEQ_INDEX, i);
        out(VGA_SEQ_DATA, saved_vga.sequencer[i]);
    }
    out(VGA_CRTC_INDEX, VGA_CRTC_PROTECT);
    out(VGA_CRTC_DATA, saved_vga.crtc[VGA_CRTC_PROTECT] & 0x7F);
    for (uint8_t i = 0; i < sizeof(saved_vga.crtc); i++) {
        out(VGA_CRTC_INDEX, i);
        out(VGA_CRTC_DATA, saved_vga.crtc[i]);
    }
    for (uint8_t i = 0; i < sizeof(saved_vga.graphics); i++) {
        out(VGA_GC_INDEX, i);
        out(VGA_GC_DATA, saved_vga.graphics[i]);
    }
    for (uint8_t i = 0; i < sizeof(saved_vga.attribute); i++) {
        in(VGA_INPUT_STATUS);
        out(VGA_AC_INDEX, i);
        out(VGA_AC_INDEX, saved_vga.attribute[i]);
    }
    in(VGA_INPUT_STATUS);
    out(VGA_AC_INDEX, VGA_AC_ENABLE);
}

static void vga_read_font(void) {
    out(VGA_SEQ_INDEX, 0x02);
    out(VGA_SEQ_DATA, 0x04);
    out(VGA_SEQ_INDEX, 0x04);
    out(VGA_SEQ_DATA, 0x07);
    out(VGA_GC_INDEX, 0x04);
    out(VGA_GC_DATA, 0x02);
    out(VGA_GC_INDEX, 0x05);
    out(VGA_GC_DATA, 0x00);
    out(VGA_GC_INDEX, 0x06);
    out(VGA_GC_DATA, 0x04);

    for (uint32_t c = 0; c < 256; c++)
        for (uint32_t row = 0; row < GRAPHICS_FONT_HEIGHT; row++)
            font[c * GRAPHICS_FONT_HEIGHT + row] = VGA_PLANE_WINDOW[c * VGA_FONT_GLYPH_STRIDE + row];

    vga_restore_registers();
    font_loaded = true;
}

void graphics_get_font(uint8_t *out) {
    for (uint32_t i = 0; i < GRAPHICS_FONT_SIZE; i++)
        out[i] = font_loaded ? font[i] : 0;
}

void graphics_init(void) {
    for (uint8_t device = 0; device < 32; device++) {
        if (pci_read(0, device, 0x00) != BGA_PCI_ID)
            continue;
        uint32_t lfb = pci_read(0, device, 0x10) & 0xFFFFFFF0u;
        uint32_t window = lfb & ~(PAGE_4MB_SIZE - 1);
        uint32_t size = GRAPHICS_WIDTH * (GRAPHICS_HEIGHT + GRAPHICS_Y_OFFSET) * (GRAPHICS_BPP / 8);
        paging_map_kernel_window(GRAPHICS_WINDOW_BASE, window, lfb - window + size);
        framebuffer = (uint32_t *) (GRAPHICS_WINDOW_BASE + lfb - window) + GRAPHICS_Y_OFFSET * GRAPHICS_WIDTH;
        return;
    }
}

bool graphics_set_mode(bool enable) {
    if (framebuffer == NULL)
        return false;
    if (enable == active)
        return true;

    if (enable) {
        vga_save_registers();
        if (!font_loaded)
            vga_read_font();
        bga_write(BGA_INDEX_ENABLE, 0);
        bga_write(BGA_INDEX_XRES, GRAPHICS_WIDTH);
        bga_write(BGA_INDEX_YRES, GRAPHICS_HEIGHT);
        bga_write(BGA_INDEX_BPP, GRAPHICS_BPP);
        bga_write(BGA_INDEX_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED | BGA_NO_CLEAR_MEM);
        bga_write(BGA_INDEX_VIRT_WIDTH, GRAPHICS_WIDTH);
        bga_write(BGA_INDEX_VIRT_HEIGHT, GRAPHICS_HEIGHT + GRAPHICS_Y_OFFSET);
        bga_write(BGA_INDEX_Y_OFFSET, GRAPHICS_Y_OFFSET);
        for (uint32_t i = 0; i < GRAPHICS_WIDTH * GRAPHICS_HEIGHT; i++)
            framebuffer[i] = 0;
    } else {
        bga_write(BGA_INDEX_ENABLE, 0);
        vga_restore_registers();
    }
    active = enable;
    return true;
}

bool graphics_is_active(void) {
    return active;
}

void graphics_blit(const struct BlitRequest *request) {
    if (!active)
        return;

    int32_t x0 = request->x < 0 ? 0 : request->x;
    int32_t y0 = request->y < 0 ? 0 : request->y;
    int32_t x1 = request->x + (int32_t) request->width;
    int32_t y1 = request->y + (int32_t) request->height;
    if (x1 > GRAPHICS_WIDTH)
        x1 = GRAPHICS_WIDTH;
    if (y1 > GRAPHICS_HEIGHT)
        y1 = GRAPHICS_HEIGHT;
    if (x0 >= x1 || y0 >= y1)
        return;

    for (int32_t y = y0; y < y1; y++) {
        const uint32_t *src = request->pixels + (y - request->y) * request->width + (x0 - request->x);
        uint32_t *dest = framebuffer + y * GRAPHICS_WIDTH + x0;
        uint32_t count = x1 - x0;
        __asm__ volatile("rep movsl" : "+S"(src), "+D"(dest), "+c"(count) : /* <Empty> */ : "memory");
    }
}
