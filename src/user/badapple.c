#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nosleep.h>

#define ROOT_INODE      1
#define CHUNK_SIZE      65536
#define CHUNK_MAX       16
#define MIN_MATCH       4
#define SCALE           6
#define VIDEO_X         40
#define VIDEO_Y         16
#define STATUS_Y        570
#define CLOSE_X         768
#define CLOSE_Y         8
#define CLOSE_SIZE      24
#define CURSOR_SIZE     16
#define SCANCODE_ESCAPE 0x01

#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xF4F1FF
#define COLOR_AMBER 0xFFB000
#define COLOR_RED   0xE0353B
#define COLOR_INK   0x0A0816

static const char *const art_cursor[CURSOR_SIZE] = {
    "k...........", "kk..........", "kwk.........", "kwwk........",
    "kwwwk.......", "kwwwwk......", "kwwwwwk.....", "kwwwwwwk....",
    "kwwwwwwwk...", "kwwwwwwwwk..", "kwwwwwkkkkk.", "kwwkwwk.....",
    "kwk.kwwk....", "kk..kwwk....", "k....kwwk...", "......kk....",
};

struct VideoHeader {
    char     magic[4];
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint16_t reserved;
    uint32_t frames;
    uint32_t packed_size;
    uint32_t runs_size;
} __attribute__((packed));

#define STRIP_PIXELS (800 * GRAPHICS_FONT_HEIGHT)

static uint8_t  *font;
static uint32_t *strip;
static char status_text[64];

static uint32_t find_chunk_size(const uint8_t *dir_buf, uint32_t dir_size, const char *name) {
    uint8_t name_len = (uint8_t) strlen(name);
    for (uint32_t blk = 0; blk + BLOCK_SIZE <= dir_size; blk += BLOCK_SIZE) {
        const uint8_t *block = dir_buf + blk;
        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            const struct EXT2DirectoryEntry *entry = (const struct EXT2DirectoryEntry *) (block + offset);
            if (entry->inode == 0 || entry->rec_len < sizeof(struct EXT2DirectoryEntry) ||
                offset + entry->rec_len > BLOCK_SIZE)
                break;
            if (entry->name_len > entry->rec_len - sizeof(struct EXT2DirectoryEntry))
                break;
            offset += entry->rec_len;
            if (entry->file_type != EXT2_FT_DIR && entry->name_len == name_len &&
                memcmp(entry + 1, name, name_len) == 0)
                return sys_inode_size(entry->inode);
        }
    }
    return 0;
}

static uint32_t read_chunks(uint8_t *buffer) {
    uint8_t dir_buf[4 * BLOCK_SIZE];
    uint32_t dir_size = sys_inode_size(ROOT_INODE);
    if (dir_size == 0)
        dir_size = BLOCK_SIZE;
    if (dir_size > sizeof(dir_buf))
        dir_size = sizeof(dir_buf);
    if (sys_read_directory(ROOT_INODE, dir_buf, dir_size) != 0)
        return 0;

    uint32_t total = 0;
    for (int i = 0; i < CHUNK_MAX; i++) {
        char name[16];
        snprintf(name, sizeof(name), "badapple.%d", i);
        uint32_t chunk_size = find_chunk_size(dir_buf, dir_size, name);
        if (chunk_size == 0 || chunk_size > CHUNK_SIZE || total + chunk_size > CHUNK_SIZE * CHUNK_MAX)
            break;
        if (sys_read_file(name, ROOT_INODE, buffer + total, chunk_size) != 0)
            break;
        total += chunk_size;
    }
    return total;
}

static uint32_t read_length(const uint8_t **in, const uint8_t *in_end, uint32_t length, bool *ok) {
    if (length == 15) {
        uint8_t extra;
        do {
            if (*in >= in_end) {
                *ok = false;
                return 0;
            }
            extra = *(*in)++;
            length += extra;
        } while (extra == 255);
    }
    return length;
}

static bool unpack(const uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size) {
    const uint8_t *in_end = in + in_size;
    uint8_t *out_start = out;
    uint8_t *end = out + out_size;
    while (out < end) {
        if (in >= in_end)
            return false;
        uint8_t token = *in++;
        bool ok = true;
        uint32_t literals = read_length(&in, in_end, token >> 4, &ok);
        if (!ok || literals > (uint32_t) (in_end - in) || literals > (uint32_t) (end - out))
            return false;
        memcpy(out, in, literals);
        out += literals;
        in += literals;
        if (out >= end)
            return out == end;

        if (in_end - in < 2)
            return false;
        uint32_t offset = in[0] | (in[1] << 8);
        in += 2;
        if (offset == 0 || offset > (uint32_t) (out - out_start))
            return false;
        uint32_t length = read_length(&in, in_end, token & 15, &ok) + MIN_MATCH;
        if (!ok || length > (uint32_t) (end - out))
            return false;
        for (; length > 0; length--, out++)
            *out = out[-offset];
    }
    return out == end;
}

static const uint8_t *decode_frame(const uint8_t *runs, const uint8_t *runs_end, uint8_t *pixels, uint32_t count) {
    uint8_t color = 0;
    for (uint32_t filled = 0; filled < count; color ^= 1) {
        if (runs >= runs_end)
            return NULL;
        uint32_t length = *runs++;
        if (length & 0x80) {
            if (runs >= runs_end)
                return NULL;
            length = ((length & 0x7F) << 8) | *runs++;
        }
        if (length > count - filled)
            return NULL;
        memset(pixels + filled, color, length);
        filled += length;
    }
    return runs;
}

static bool draw_changed_rows(const struct VideoHeader *video, const uint8_t *pixels, uint8_t *shown,
                              int32_t cursor_y) {
    uint32_t width = video->width * SCALE;
    bool hit_cursor = false;
    for (uint32_t y = 0; y < video->height; y++) {
        const uint8_t *row = pixels + y * video->width;
        if (memcmp(row, shown + y * video->width, video->width) == 0)
            continue;
        for (uint32_t x = 0; x < width; x++)
            strip[x] = row[x / SCALE] ? COLOR_WHITE : COLOR_BLACK;
        for (uint32_t line = 1; line < SCALE; line++)
            memcpy(strip + line * width, strip, width * sizeof(uint32_t));
        int32_t dest_y = VIDEO_Y + (int32_t) (y * SCALE);
        sys_blit(strip, VIDEO_X, dest_y, width, SCALE);
        memcpy(shown + y * video->width, row, video->width);
        if (dest_y < cursor_y + CURSOR_SIZE && dest_y + SCALE > cursor_y)
            hit_cursor = true;
    }
    return hit_cursor;
}

static void draw_text(int32_t x, int32_t y, const char *text, uint32_t fg, uint32_t bg) {
    uint32_t width = strlen(text) * 8;
    for (uint32_t row = 0; row < GRAPHICS_FONT_HEIGHT; row++)
        for (uint32_t col = 0; col < width; col++) {
            uint8_t glyph = font[(uint8_t) text[col / 8] * GRAPHICS_FONT_HEIGHT + row];
            strip[row * width + col] = glyph & (0x80 >> (col % 8)) ? fg : bg;
        }
    sys_blit(strip, x, y, width, GRAPHICS_FONT_HEIGHT);
}

static void draw_close_button(void) {
    for (int i = 0; i < CLOSE_SIZE * CLOSE_SIZE; i++)
        strip[i] = COLOR_RED;
    sys_blit(strip, CLOSE_X, CLOSE_Y, CLOSE_SIZE, CLOSE_SIZE);
    draw_text(CLOSE_X + 8, CLOSE_Y + 4, "x", COLOR_WHITE, COLOR_RED);
}

static void draw_status_bar(void) {
    draw_text(VIDEO_X, STATUS_Y, "BAD APPLE!!", COLOR_AMBER, COLOR_BLACK);
    if (status_text[0] != '\0')
        draw_text(VIDEO_X + 160, STATUS_Y, status_text, COLOR_WHITE, COLOR_BLACK);
    draw_text(VIDEO_X + 560, STATUS_Y, "Esc to close", COLOR_WHITE, COLOR_BLACK);
}

static void draw_screen(const struct GraphicsInfo *info) {
    for (uint32_t y = 0; y < info->height; y += GRAPHICS_FONT_HEIGHT)
        sys_blit(strip, 0, y, info->width, GRAPHICS_FONT_HEIGHT);
    sys_graphics_font(font);
    draw_close_button();
    draw_status_bar();
}

static void blit_cursor_region(int32_t cx, int32_t cy, const struct VideoHeader *video, const uint8_t *shown,
                               bool draw_arrow) {
    int32_t x0 = cx < 0 ? 0 : cx;
    int32_t y0 = cy < 0 ? 0 : cy;
    int32_t x1 = cx + CURSOR_SIZE > 800 ? 800 : cx + CURSOR_SIZE;
    int32_t y1 = cy + CURSOR_SIZE > 600 ? 600 : cy + CURSOR_SIZE;
    if (x0 >= x1 || y0 >= y1)
        return;

    uint32_t pw = (uint32_t) (x1 - x0);
    uint32_t ph = (uint32_t) (y1 - y0);
    uint32_t patch[CURSOR_SIZE * CURSOR_SIZE];
    int32_t vx1 = VIDEO_X + (int32_t) (video->width * SCALE);
    int32_t vy1 = VIDEO_Y + (int32_t) (video->height * SCALE);

    for (int32_t py = y0; py < y1; py++) {
        for (int32_t px = x0; px < x1; px++) {
            uint32_t color = COLOR_BLACK;
            if (px >= VIDEO_X && px < vx1 && py >= VIDEO_Y && py < vy1) {
                uint32_t sy = (uint32_t) (py - VIDEO_Y) / SCALE;
                uint32_t sx = (uint32_t) (px - VIDEO_X) / SCALE;
                color = shown[sy * video->width + sx] ? COLOR_WHITE : COLOR_BLACK;
            } else if (px >= CLOSE_X && px < CLOSE_X + CLOSE_SIZE &&
                       py >= CLOSE_Y && py < CLOSE_Y + CLOSE_SIZE) {
                color = COLOR_RED;
            }
            if (draw_arrow && px - cx < 12) {
                char key = art_cursor[py - cy][px - cx];
                if (key == 'k')
                    color = COLOR_INK;
                else if (key == 'w')
                    color = COLOR_WHITE;
            }
            patch[(uint32_t) (py - y0) * pw + (uint32_t) (px - x0)] = color;
        }
    }

    sys_blit(patch, x0, y0, pw, ph);
    if (!draw_arrow) {
        if (x1 > CLOSE_X && x0 < CLOSE_X + CLOSE_SIZE && y1 > CLOSE_Y && y0 < CLOSE_Y + CLOSE_SIZE)
            draw_close_button();
        if (y1 > STATUS_Y && y0 < STATUS_Y + GRAPHICS_FONT_HEIGHT)
            draw_status_bar();
    }
}

static bool close_requested(struct MouseState *last_mouse) {
    uint16_t event;
    while ((event = sys_key_event()) != 0)
        if (!(event & KEY_EVENT_RELEASED) && (event & 0x7F) == SCANCODE_ESCAPE)
            return true;

    struct MouseState mouse;
    sys_mouse(&mouse);
    bool clicked = mouse.left_presses != last_mouse->left_presses;
    *last_mouse = mouse;
    return clicked && mouse.x >= CLOSE_X && mouse.x < CLOSE_X + CLOSE_SIZE &&
           mouse.y >= CLOSE_Y && mouse.y < CLOSE_Y + CLOSE_SIZE;
}

static void draw_status(const struct VideoHeader *video, uint32_t frame, uint32_t fps) {
    uint32_t seconds = frame / video->fps, total = video->frames / video->fps;
    snprintf(status_text, sizeof(status_text), "%02u:%02u / %02u:%02u   FPS %2u   ", seconds / 60,
             seconds % 60, total / 60, total % 60, fps);
    draw_text(VIDEO_X + 160, STATUS_Y, status_text, COLOR_WHITE, COLOR_BLACK);
}

static void play(const struct VideoHeader *video, const uint8_t *runs, uint8_t *pixels, uint8_t *shown) {
    struct MouseState mouse;
    sys_mouse(&mouse);
    while (sys_key_event() != 0)
        ;

    int32_t cur_x = mouse.x, cur_y = mouse.y;
    blit_cursor_region(cur_x, cur_y, video, shown, true);

    const uint8_t *runs_end = runs + video->runs_size;
    uint32_t period = 1000 / video->fps;
    uint32_t start = sys_ticks(), second_start = start, drawn = 0, fps = 0;
    for (uint32_t frame = 0; frame < video->frames; frame++) {
        runs = decode_frame(runs, runs_end, pixels, video->width * video->height);
        if (runs == NULL)
            return;
        uint32_t due = start + frame * period;
        if ((int32_t) (sys_ticks() - due) < (int32_t) period) {
            if (draw_changed_rows(video, pixels, shown, cur_y))
                blit_cursor_region(cur_x, cur_y, video, shown, true);
            drawn++;
        }
        if (sys_ticks() - second_start >= 1000) {
            fps = drawn;
            drawn = 0;
            second_start += 1000;
        }
        if (frame % video->fps == 0) {
            draw_status(video, frame, fps);
            if (cur_y < STATUS_Y + GRAPHICS_FONT_HEIGHT && cur_y + CURSOR_SIZE > STATUS_Y)
                blit_cursor_region(cur_x, cur_y, video, shown, true);
        }

        do {
            if (close_requested(&mouse))
                return;
            if (mouse.x != cur_x || mouse.y != cur_y) {
                blit_cursor_region(cur_x, cur_y, video, shown, false);
                cur_x = mouse.x;
                cur_y = mouse.y;
                blit_cursor_region(cur_x, cur_y, video, shown, true);
            }
            sys_yield();
        } while ((int32_t) (sys_ticks() - (due + period)) < 0);
    }
}

int main(void) {
    (void) sys_key_event();
    uint8_t *packed = malloc(CHUNK_SIZE * CHUNK_MAX);
    uint32_t loaded = packed != NULL ? read_chunks(packed) : 0;
    struct VideoHeader video;
    if (loaded >= sizeof(video))
        memcpy(&video, packed, sizeof(video));
    if (loaded < sizeof(video) || memcmp(video.magic, "BAPL", 4) != 0 ||
        video.packed_size > loaded - sizeof(video) ||
        video.width == 0 || video.height == 0 || video.fps == 0 || video.frames == 0 ||
        video.runs_size == 0 ||
        video.width * SCALE > 800 || VIDEO_Y + video.height * SCALE > STATUS_Y) {
        printf("badapple: video files badapple.0, badapple.1, ... not found\n");
        free(packed);
        return 1;
    }

    uint32_t frame_size = (uint32_t) video.width * video.height;
    uint8_t *runs = malloc(video.runs_size);
    uint8_t *pixels = malloc(frame_size);
    uint8_t *shown = calloc(frame_size, 1);
    font  = malloc(GRAPHICS_FONT_SIZE);
    strip = calloc(STRIP_PIXELS, sizeof(uint32_t));

    int result = 1;
    struct GraphicsInfo info;
    if (runs == NULL || pixels == NULL || shown == NULL || font == NULL || strip == NULL) {
        printf("badapple: not enough memory\n");
    } else if (!unpack(packed + sizeof(video), video.packed_size, runs, video.runs_size)) {
        printf("badapple: corrupt video data\n");
    } else if (!sys_graphics_mode(true, &info)) {
        printf("badapple: graphics mode not available\n");
    } else {
        draw_screen(&info);
        play(&video, runs, pixels, shown);
        result = 0;
    }

    free(packed);
    free(runs);
    free(pixels);
    free(shown);
    free(font);
    free(strip);
    return result;
}
