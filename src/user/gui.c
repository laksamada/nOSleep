#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nosleep.h>

#define SCREEN_WIDTH    800
#define SCREEN_HEIGHT   600
#define TASKBAR_TOP     568
#define CHAR_WIDTH      8
#define CHAR_HEIGHT     GRAPHICS_FONT_HEIGHT
#define ART_SIZE        16
#define ICON_SCALE      3
#define ICON_SPACING    96
#define TITLE_HEIGHT    24
#define CLOSE_SIZE      20
#define FILE_ROW_HEIGHT   26
#define FILE_VISIBLE_ROWS 8
#define FILE_ITEMS_MAX    64
#define STAR_COUNT      46
#define BUILDING_MAX    24
#define LIGHT_MAX       160
#define SPLASH_MS       1200
#define DOUBLE_CLICK_MS 500
#define CURSOR_BLINK_MS 500
#define FRAME_MS        16
#define ROOT_INODE      1
#define PROCESS_LIST_MAX 16
#define TRANSPARENT     0xFFFFFFFFu
#define ESCAPE_COOLDOWN_MS 300u

#define SCANCODE_ESCAPE 0x01
#define SCANCODE_TAB    0x0F
#define SCANCODE_ENTER  0x1C
#define SCANCODE_UP     0x48
#define SCANCODE_DOWN   0x50

#define COLOR_INK          0x0A0816
#define COLOR_WHITE        0xF4F1FF
#define COLOR_AMBER        0xFFB000
#define COLOR_AMBER_DARK   0xB87800
#define COLOR_LILAC        0xC9C3F0
#define COLOR_LILAC_MUTED  0x8F8AC4
#define COLOR_CITY         0x070914
#define COLOR_TASKBAR      0x0B0A1E
#define COLOR_TITLE        0x2A2366
#define COLOR_TITLE_STRIPE 0x3B3488
#define COLOR_IDLE_TITLE   0x3A3852
#define COLOR_IDLE_STRIPE  0x4A4866
#define COLOR_BEVEL_LIGHT  0x4A43A0
#define COLOR_BEVEL_DARK   0x050411
#define COLOR_SUNKEN       0x141236
#define COLOR_FACE         0xE6E1F2
#define COLOR_FACE_ZEBRA   0xDCD6EC
#define COLOR_FACE_SHADOW  0x8C86AE
#define COLOR_TEXT         0x1A1830
#define COLOR_ART_BOX      0x141A3C
#define COLOR_TERMINAL_BG  0x07061A

static const uint32_t sky_bands[] = {
    0x060918, 0x090E24, 0x0D1430, 0x111A3C, 0x162048, 0x1B2654, 0x212D60, 0x28346C,
};

static const uint32_t terminal_colors[16] = {
    COLOR_TERMINAL_BG, 0x3B3488, 0x2E9E5B, 0x2AA198, 0xC0392B, 0x8E44AD, 0xB87800, COLOR_LILAC,
    0x5C5878, 0x6C63FF, 0x7CFC9A, 0x7FE3FF, 0xFF6B6B, 0xE39BFF, COLOR_AMBER, COLOR_WHITE,
};

static const char *const art_apple[ART_SIZE] = {
    ".......kk.......", "........k.......", "..kkkk.k.gggg...", ".kwwwwwkkkkkkgg.",
    "kwwwwwwkkkkkkkkg", "kwwwwwwkkkkkkkkg", "kwwwwwwkkkkkkkkg", "kwwwwwwkkkkkkkkg",
    "kwwwwwwkkkkkkkkg", "kwwwwwwkkkkkkkkg", ".kwwwwwkkkkkkkg.", ".kwwwwwkkkkkkkg.",
    "..kwwwwkkkkkkg..", "...kwwwkkkkkg...", "....kk...gg.....", "................",
};

static const char *const art_folder[ART_SIZE] = {
    "................", ".kkkkk..........", "kfffffk.........", "kfffffkkkkkkkkk.",
    "kFFFFFFFFFFFFFFk", "kffffffffffffffk", "kffffffffffffffk", "kffffffffffffffk",
    "kffffffffffffffk", "kffffffffffffffk", "kffffffffffffffk", "kffffffffffffffk",
    "kFFFFFFFFFFFFFFk", ".kkkkkkkkkkkkkk.", "................", "................",
};

static const char *const art_mug[ART_SIZE] = {
    "....w..w..w.....", "...w..w..w......", "....w..w..w.....", "................",
    ".kkkkkkkkkkk....", ".kcccccccccck...", ".kbbbbbbbbbbkkk.", ".kbbbbbbbbbbk.kk",
    ".kwwwwwwwwwwk..k", ".kwwwwwwwwwwk..k", ".kwwyywwwwwwk.kk", ".kwwyywwwwwwkkk.",
    ".kwwwwwwwwwwk...", "..kwwwwwwwwk....", "...kkkkkkkk.....", "................",
};

static const char *const art_terminal[ART_SIZE] = {
    "kkkkkkkkkkkkkkkk", "kggggggggggggggk", "kgkkkkkkkkkkkkgk", "kgknnnnnnnnnnkgk",
    "kgknynnnnnnnnkgk", "kgknnynnnnnnnkgk", "kgknynnyyynnnkgk", "kgknnnnnnnnnnkgk",
    "kgknnnnnnnnnnkgk", "kgkkkkkkkkkkkkgk", "kggggggggggggggk", "kkkkkkkkkkkkkkkk",
    "....kGGGGGGk....", "..kkkkkkkkkkkk..", "..kggggggggggk..", "..kkkkkkkkkkkk..",
};

static const char *const art_moon[ART_SIZE] = {
    ".....kkkk.......", "...kkmmmmk......", "..kmmmmmk.......", ".kmmmmmk........",
    ".kmmmmk.........", "kmmmmmk.........", "kmmmMk..........", "kmmmmk..........",
    "kmmMmk..........", "kmmmmmk.........", ".kmmmmk.........", ".kmmmmmk......k.",
    "..kmmmmmkk..kkm.", "...kkmmmmmmmmk..", ".....kkkkkkkk...", "................",
};

static const char *const art_text_file[ART_SIZE] = {
    "kkkkkkkkk.......", "kwwwwwwwkk......", "kwwwwwwwkwk.....", "kwwwwwwwkkkk....",
    "kwggggggwwwk....", "kwwwwwwwwwwk....", "kwggggggggwk....", "kwwwwwwwwwwk....",
    "kwggggggggwk....", "kwwwwwwwwwwk....", "kwggggwwwwwk....", "kwwwwwwwwwwk....",
    "kkkkkkkkkkkk....", "................", "................", "................",
};

static const char *const art_cursor[ART_SIZE] = {
    "k...........", "kk..........", "kwk.........", "kwwk........",
    "kwwwk.......", "kwwwwk......", "kwwwwwk.....", "kwwwwwwk....",
    "kwwwwwwwk...", "kwwwwwwwwk..", "kwwwwwkkkkk.", "kwwkwwk.....",
    "kwk.kwwk....", "kk..kwwk....", "k....kwwk...", "......kk....",
};

enum WindowKind {
    WINDOW_TERMINAL,
    WINDOW_FILES,
    WINDOW_ABOUT,
    WINDOW_COUNT,
};

enum Action {
    ACTION_BAD_APPLE,
    ACTION_FILES,
    ACTION_ABOUT,
    ACTION_TERMINAL,
};

struct Icon {
    const char        *label;
    const char *const *art;
    enum Action        action;
};

static const struct Icon icons[] = {
    {"Bad Apple", art_apple,    ACTION_BAD_APPLE},
    {"Files",     art_folder,   ACTION_FILES},
    {"About",     art_mug,      ACTION_ABOUT},
    {"Terminal",  art_terminal, ACTION_TERMINAL},
};

#define ICON_COUNT ((int) (sizeof(icons) / sizeof(icons[0])))

struct Window {
    bool        open;
    int32_t     x, y, width, height;
    const char *title;
    const char *task_label;
};

static struct Window windows[WINDOW_COUNT] = {
    [WINDOW_TERMINAL] = {false, 104, 40, 656, 440, "Terminal", "Terminal"},
    [WINDOW_FILES]    = {false, 206, 88, 424, 318, "Files in /", "Files"},
    [WINDOW_ABOUT]    = {false, 180, 128, 440, 270, "About nOSleep", "About"},
};

struct FileLine {
    char               name[32];
    char               type[12];
    char               size[16];
    const char *const *art;
};

struct Rect {
    int32_t  x, y, width, height;
    uint32_t color;
};

static uint32_t *screen;
static uint8_t font[GRAPHICS_FONT_SIZE];
static struct TextScreen terminal_text;
static int window_stack[WINDOW_COUNT], window_stack_size;
static int focus = -1;
static struct FileLine files[FILE_ITEMS_MAX];
static int file_count, file_scroll = 0, selected_file = -1, selected_icon;
static uint32_t total_file_bytes;
static uint32_t ignore_escape_until = 0;
static struct Rect stars[STAR_COUNT], buildings[BUILDING_MAX], lights[LIGHT_MAX];
static int building_count, light_count;
static uint32_t random_state;

static uint32_t random_next(void) {
    random_state = (random_state * 1103515245u + 12345u) & 0x7FFFFFFFu;
    return random_state;
}

static int32_t random_below(int32_t limit) {
    return (int32_t) (((uint64_t) random_next() * (uint32_t) limit) >> 31);
}

static bool random_chance(uint32_t percent) {
    return random_below(100) < (int32_t) percent;
}

static void build_scenery(void) {
    random_state = 7;
    for (int i = 0; i < STAR_COUNT; i++) {
        int32_t size = random_chance(20) ? 2 : 1;
        uint32_t color = random_chance(30) ? 0x9DB4FF : COLOR_WHITE;
        stars[i] = (struct Rect) {120 + random_below(670), 8 + random_below(400), size, size, color};
    }

    random_state = 21;
    for (int32_t x = 0; x < SCREEN_WIDTH && building_count < BUILDING_MAX;) {
        int32_t width = 40 + random_below(50), height = 40 + random_below(70);
        int32_t top = TASKBAR_TOP - height;
        buildings[building_count++] = (struct Rect) {x, top, width - 4, height, COLOR_CITY};
        for (int32_t wy = top + 8; wy < TASKBAR_TOP - 8; wy += 12)
            for (int32_t wx = x + 6; wx < x + width - 12; wx += 10)
                if (random_chance(7) && light_count < LIGHT_MAX)
                    lights[light_count++] = (struct Rect) {wx, wy, 4, 6, COLOR_AMBER};
        x += width;
    }
}

static void fill_rect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + width > SCREEN_WIDTH ? SCREEN_WIDTH : x + width;
    int32_t y1 = y + height > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + height;
    for (int32_t row = y0; row < y1; row++)
        for (int32_t col = x0; col < x1; col++)
            screen[row * SCREEN_WIDTH + col] = color;
}

static void draw_rect(const struct Rect *rect) {
    fill_rect(rect->x, rect->y, rect->width, rect->height, rect->color);
}

static void draw_frame(int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, uint32_t color) {
    fill_rect(x, y, width, thickness, color);
    fill_rect(x, y + height - thickness, width, thickness, color);
    fill_rect(x, y, thickness, height, color);
    fill_rect(x + width - thickness, y, thickness, height, color);
}

static void draw_bevel(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t face, uint32_t light, uint32_t dark) {
    fill_rect(x, y, width, height, face);
    fill_rect(x, y, width, 2, light);
    fill_rect(x, y, 2, height, light);
    fill_rect(x, y + height - 2, width, 2, dark);
    fill_rect(x + width - 2, y, 2, height, dark);
}

static uint32_t art_color(char key) {
    switch (key) {
        case 'k': return COLOR_INK;
        case 'w': return COLOR_WHITE;
        case 'r': return 0xE0353B;
        case 'y': return COLOR_AMBER;
        case 'b': return 0x6B3E26;
        case 'c': return 0xE8D3B0;
        case 'g': return 0x9B97B5;
        case 'G': return 0x5C5878;
        case 'f': return 0xE7B33D;
        case 'F': return 0xA97A1F;
        case 'm': return 0xF4E9C1;
        case 'M': return 0xC9B98A;
        case 'n': return COLOR_ART_BOX;
        default:  return TRANSPARENT;
    }
}

static void draw_art(const char *const *art, int32_t x, int32_t y, int32_t scale) {
    for (int32_t row = 0; row < ART_SIZE; row++)
        for (int32_t col = 0; art[row][col] != '\0'; col++) {
            uint32_t color = art_color(art[row][col]);
            if (color != TRANSPARENT)
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
}

static void draw_char(int32_t x, int32_t y, char c, uint32_t color, int32_t scale) {
    const uint8_t *glyph = &font[(uint8_t) c * CHAR_HEIGHT];
    for (int32_t row = 0; row < CHAR_HEIGHT; row++)
        for (int32_t col = 0; col < CHAR_WIDTH; col++)
            if (glyph[row] & (0x80 >> col))
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
}

static void draw_text_scaled(int32_t x, int32_t y, const char *text, uint32_t color, int32_t scale) {
    for (; *text != '\0'; text++, x += CHAR_WIDTH * scale)
        draw_char(x, y, *text, color, scale);
}

static void draw_text(int32_t x, int32_t y, const char *text, uint32_t color) {
    draw_text_scaled(x, y, text, color, 1);
}

static int32_t text_width(const char *text, int32_t scale) {
    return (int32_t) strlen(text) * CHAR_WIDTH * scale;
}

static void draw_logo(int32_t right, int32_t y, int32_t scale) {
    int32_t x = right - text_width("nOSleep", scale);
    draw_text_scaled(x, y, "n", COLOR_LILAC, scale);
    draw_text_scaled(x + text_width("n", scale), y, "OS", COLOR_AMBER, scale);
    draw_text_scaled(x + text_width("nOS", scale), y, "leep", COLOR_LILAC, scale);
}

static void format_duration(char *out, uint32_t size, uint32_t ms) {
    uint32_t seconds = ms / 1000;
    snprintf(out, size, "%02u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

static bool inside(int32_t px, int32_t py, int32_t x, int32_t y, int32_t width, int32_t height) {
    return px >= x && px < x + width && py >= y && py < y + height;
}

static void set_focus(int window) {
    focus = window;
    sys_keyboard_focus(focus == WINDOW_TERMINAL);
}

static void raise_window(int window) {
    int position = 0;
    for (int i = 0; i < window_stack_size; i++)
        if (window_stack[i] != window)
            window_stack[position++] = window_stack[i];
    window_stack[position] = window;
    window_stack_size = position + 1;
    windows[window].open = true;
    set_focus(window);
}

static void close_window(int window) {
    int position = 0;
    for (int i = 0; i < window_stack_size; i++)
        if (window_stack[i] != window)
            window_stack[position++] = window_stack[i];
    window_stack_size = position;
    windows[window].open = false;
    set_focus(window_stack_size > 0 ? window_stack[window_stack_size - 1] : -1);
}

static int window_at(int32_t x, int32_t y) {
    for (int i = window_stack_size - 1; i >= 0; i--) {
        const struct Window *window = &windows[window_stack[i]];
        if (inside(x, y, window->x, window->y, window->width, window->height))
            return window_stack[i];
    }
    return -1;
}

static int32_t close_button_x(const struct Window *window) {
    return window->x + window->width - CLOSE_SIZE - 8;
}

static bool on_close_button(const struct Window *window, int32_t x, int32_t y) {
    return inside(x, y, close_button_x(window), window->y + 6, CLOSE_SIZE, CLOSE_SIZE);
}

static bool on_title_bar(const struct Window *window, int32_t x, int32_t y) {
    return inside(x, y, window->x, window->y, window->width, TITLE_HEIGHT + 4);
}

static bool on_ok_button(int32_t x, int32_t y) {
    const struct Window *window = &windows[WINDOW_ABOUT];
    return inside(x, y, window->x + window->width - 104, window->y + window->height - 44, 88, 28);
}

static int files_max_scroll(void) {
    return file_count > FILE_VISIBLE_ROWS ? file_count - FILE_VISIBLE_ROWS : 0;
}

static void clamp_file_scroll(void) {
    int max_scroll = files_max_scroll();
    if (file_scroll < 0)
        file_scroll = 0;
    if (file_scroll > max_scroll)
        file_scroll = max_scroll;
}

static void ensure_file_visible(int index) {
    if (index < 0 || index >= file_count)
        return;
    if (index < file_scroll)
        file_scroll = index;
    else if (index >= file_scroll + FILE_VISIBLE_ROWS)
        file_scroll = index - FILE_VISIBLE_ROWS + 1;
    clamp_file_scroll();
}

static int file_row_at(int32_t x, int32_t y) {
    const struct Window *window = &windows[WINDOW_FILES];
    int32_t list_width = window->width - 24 - (file_count > FILE_VISIBLE_ROWS ? 22 : 0);
    if (!inside(x, y, window->x + 12, window->y + 68, list_width, FILE_VISIBLE_ROWS * FILE_ROW_HEIGHT))
        return -1;
    int row = (y - window->y - 68) / FILE_ROW_HEIGHT;
    return file_scroll + row < file_count ? file_scroll + row : -1;
}

static int32_t icon_top(int index) {
    return 24 + index * ICON_SPACING;
}

static int icon_at(int32_t x, int32_t y) {
    for (int i = 0; i < ICON_COUNT; i++)
        if (inside(x, y, 12, icon_top(i), 80, 84))
            return i;
    return -1;
}

static int32_t task_button_x(int slot) {
    return 128 + slot * 116;
}

static int task_button_at(int32_t x, int32_t y) {
    int slot = 0;
    for (int window = 0; window < WINDOW_COUNT; window++) {
        if (!windows[window].open)
            continue;
        if (inside(x, y, task_button_x(slot), TASKBAR_TOP + 4, 112, 24))
            return window;
        slot++;
    }
    return -1;
}

static void draw_sky(void) {
    for (uint32_t i = 0; i < sizeof(sky_bands) / sizeof(sky_bands[0]); i++)
        fill_rect(0, i * 71, SCREEN_WIDTH, 71, sky_bands[i]);
    for (int i = 0; i < STAR_COUNT; i++)
        draw_rect(&stars[i]);
}

static void draw_desktop_scenery(void) {
    draw_sky();
    draw_art(art_moon, 660, 48, 4);
    draw_logo(770, 300, 4);
    draw_text(770 - text_width("the OS that never sleeps", 1), 372, "the OS that never sleeps", COLOR_LILAC_MUTED);
    for (int i = 0; i < building_count; i++)
        draw_rect(&buildings[i]);
    for (int i = 0; i < light_count; i++)
        draw_rect(&lights[i]);
}

static void draw_icons(void) {
    for (int i = 0; i < ICON_COUNT; i++) {
        int32_t top = icon_top(i);
        draw_art(icons[i].art, 28, top + 4, ICON_SCALE);

        const char *label = icons[i].label;
        int32_t label_x = 52 - text_width(label, 1) / 2, label_y = top + 58;
        if (i == selected_icon && focus == -1) {
            fill_rect(label_x - 4, label_y, text_width(label, 1) + 8, CHAR_HEIGHT, COLOR_AMBER);
            draw_text(label_x, label_y, label, COLOR_INK);
        } else {
            draw_text(label_x + 1, label_y + 1, label, COLOR_INK);
            draw_text(label_x, label_y, label, COLOR_WHITE);
        }
    }
}

static bool is_text_extension(const char *name) {
    const char *ext = NULL;
    for (const char *p = name; *p != '\0'; p++)
        if (*p == '.')
            ext = p;
    if (ext == NULL)
        return false;
    return strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0 || strcmp(ext, ".log") == 0 ||
           strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 || strcmp(ext, ".sh") == 0;
}

static void load_files(void) {
    uint8_t dir_buf[4 * BLOCK_SIZE];
    file_count = 0;
    file_scroll = 0;
    total_file_bytes = 0;
    selected_file = -1;
    uint32_t dir_size = sys_inode_size(ROOT_INODE);
    if (dir_size == 0)
        dir_size = BLOCK_SIZE;
    if (dir_size > sizeof(dir_buf))
        dir_size = sizeof(dir_buf);
    if (sys_read_directory(ROOT_INODE, dir_buf, dir_size) != 0)
        return;

    for (uint32_t blk = 0; blk + BLOCK_SIZE <= dir_size && file_count < FILE_ITEMS_MAX; blk += BLOCK_SIZE) {
        const uint8_t *block = dir_buf + blk;
        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE && file_count < FILE_ITEMS_MAX) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (block + offset);
            if (entry->inode == 0 || entry->rec_len < sizeof(struct EXT2DirectoryEntry) ||
                offset + entry->rec_len > BLOCK_SIZE)
                break;
            if (entry->name_len > entry->rec_len - sizeof(struct EXT2DirectoryEntry))
                break;
            offset += entry->rec_len;

            const char *name = (const char *) (entry + 1);
            bool dot = (entry->name_len == 1 && name[0] == '.') ||
                       (entry->name_len == 2 && name[0] == '.' && name[1] == '.');
            if (dot)
                continue;

            struct FileLine *line = &files[file_count++];
            uint32_t len = entry->name_len < sizeof(line->name) - 1 ? entry->name_len : sizeof(line->name) - 1;
            memcpy(line->name, name, len);
            line->name[len] = '\0';

            if (entry->file_type == EXT2_FT_DIR) {
                strcpy(line->type, "folder");
                strcpy(line->size, "-");
                line->art = art_folder;
                continue;
            }
            uint32_t bytes = sys_inode_size(entry->inode);
            total_file_bytes += bytes;
            if (strchr(line->name, '.') == NULL) {
                strcpy(line->type, "program");
                line->art = art_terminal;
            } else if (is_text_extension(line->name)) {
                strcpy(line->type, "text");
                line->art = art_text_file;
            } else {
                strcpy(line->type, "data");
                line->art = art_text_file;
            }
            if (bytes < 1024)
                snprintf(line->size, sizeof(line->size), "%u B", bytes);
            else
                snprintf(line->size, sizeof(line->size), "%u.%u KB", bytes / 1024, bytes % 1024 * 10 / 1024);
        }
    }
}

static void draw_window_chrome(const struct Window *window, bool focused) {
    int32_t x = window->x, y = window->y, width = window->width, height = window->height;
    fill_rect(x + 8, y + 8, width, height, COLOR_INK);
    fill_rect(x, y, width, height, COLOR_FACE);
    draw_frame(x, y, width, height, 2, COLOR_INK);
    fill_rect(x + 2, y + 2, width - 4, 2, 0xFFFFFF);
    fill_rect(x + 2, y + 2, 2, height - 4, 0xFFFFFF);
    fill_rect(x + 2, y + height - 4, width - 4, 2, COLOR_FACE_SHADOW);
    fill_rect(x + width - 4, y + 2, 2, height - 4, COLOR_FACE_SHADOW);

    uint32_t title_color = focused ? COLOR_TITLE : COLOR_IDLE_TITLE;
    uint32_t stripe_color = focused ? COLOR_TITLE_STRIPE : COLOR_IDLE_STRIPE;
    for (int32_t row = 0; row < TITLE_HEIGHT; row++)
        fill_rect(x + 4, y + 4 + row, width - 8, 1, (row / 2) % 2 ? stripe_color : title_color);
    int32_t title_width = text_width(window->title, 1);
    int32_t title_x = x + width / 2 - title_width / 2;
    fill_rect(title_x - 10, y + 4, title_width + 20, TITLE_HEIGHT, title_color);
    draw_text(title_x, y + 8, window->title, focused ? COLOR_AMBER : COLOR_LILAC_MUTED);

    int32_t close_x = close_button_x(window);
    fill_rect(close_x, y + 6, CLOSE_SIZE, CLOSE_SIZE, COLOR_FACE);
    draw_frame(close_x, y + 6, CLOSE_SIZE, CLOSE_SIZE, 2, COLOR_INK);
    draw_text(close_x + 6, y + 7, "x", COLOR_INK);
}

static void draw_terminal_window(bool focused, bool cursor_visible) {
    const struct Window *window = &windows[WINDOW_TERMINAL];
    int32_t left = window->x + 8, top = window->y + 32;
    draw_window_chrome(window, focused);
    fill_rect(left, top, TEXT_COLUMNS * CHAR_WIDTH, TEXT_ROWS * CHAR_HEIGHT, COLOR_TERMINAL_BG);

    for (int32_t row = 0; row < TEXT_ROWS; row++)
        for (int32_t col = 0; col < TEXT_COLUMNS; col++) {
            uint8_t c = terminal_text.cells[(row * TEXT_COLUMNS + col) * 2];
            uint8_t attribute = terminal_text.cells[(row * TEXT_COLUMNS + col) * 2 + 1];
            int32_t x = left + col * CHAR_WIDTH, y = top + row * CHAR_HEIGHT;
            if (attribute >> 4)
                fill_rect(x, y, CHAR_WIDTH, CHAR_HEIGHT, terminal_colors[attribute >> 4]);
            if (c != 0 && c != ' ')
                draw_char(x, y, c, terminal_colors[attribute & 0xF], 1);
        }

    if (focused && cursor_visible)
        fill_rect(left + terminal_text.cursor_col * CHAR_WIDTH, top + terminal_text.cursor_row * CHAR_HEIGHT + 13,
                  CHAR_WIDTH, 2, COLOR_AMBER);
}

static void files_scrollbar_geom(const struct Window *window, int32_t *sb_x, int32_t *sb_y,
                                 int32_t *sb_w, int32_t *sb_h, int32_t *thumb_y, int32_t *thumb_h) {
    int32_t x = window->x + window->width - 28;
    int32_t y = window->y + 68;
    int32_t w = 16;
    int32_t h = FILE_VISIBLE_ROWS * FILE_ROW_HEIGHT;
    int32_t track_h = h - 36;
    int32_t th = (track_h * FILE_VISIBLE_ROWS) / (file_count > 0 ? file_count : 1);
    if (th < 18)
        th = 18;
    int max_scroll = files_max_scroll();
    int32_t ty = y + 18 + (max_scroll > 0 ? ((track_h - th) * file_scroll) / max_scroll : 0);

    *sb_x = x;
    *sb_y = y;
    *sb_w = w;
    *sb_h = h;
    if (thumb_y != NULL)
        *thumb_y = ty;
    if (thumb_h != NULL)
        *thumb_h = th;
}

static void draw_files_window(bool focused) {
    const struct Window *window = &windows[WINDOW_FILES];
    bool has_scroll = file_count > FILE_VISIBLE_ROWS;
    int32_t x = window->x, y = window->y;
    int32_t row_w = window->width - 24 - (has_scroll ? 22 : 0);
    int32_t name_x = x + 52, type_x = name_x + 186;
    int32_t size_right = x + window->width - 20 - (has_scroll ? 22 : 0);
    draw_window_chrome(window, focused);

    draw_text(name_x, y + 42, "Name", COLOR_TITLE);
    draw_text(type_x, y + 42, "Type", COLOR_TITLE);
    draw_text(size_right - text_width("Size", 1), y + 42, "Size", COLOR_TITLE);
    fill_rect(x + 12, y + 62, window->width - 24, 2, COLOR_FACE_SHADOW);

    for (int row = 0; row < FILE_VISIBLE_ROWS && file_scroll + row < file_count; row++) {
        int i = file_scroll + row;
        int32_t row_y = y + 68 + row * FILE_ROW_HEIGHT;
        uint32_t text_color = COLOR_TEXT;
        if (i == selected_file) {
            fill_rect(x + 12, row_y, row_w, FILE_ROW_HEIGHT, COLOR_AMBER);
            text_color = COLOR_INK;
        } else if (row % 2) {
            fill_rect(x + 12, row_y, row_w, FILE_ROW_HEIGHT, COLOR_FACE_ZEBRA);
        }
        char display_name[24];
        if (strlen(files[i].name) > 20) {
            memcpy(display_name, files[i].name, 18);
            display_name[18] = '.';
            display_name[19] = '.';
            display_name[20] = '\0';
        } else {
            strcpy(display_name, files[i].name);
        }
        draw_art(files[i].art, x + 18, row_y + 5, 1);
        draw_text(name_x, row_y + 5, display_name, text_color);
        draw_text(type_x, row_y + 5, files[i].type, text_color);
        draw_text(size_right - text_width(files[i].size, 1), row_y + 5, files[i].size, text_color);
    }

    if (has_scroll) {
        int32_t sb_x, sb_y, sb_w, sb_h, thumb_y, thumb_h;
        files_scrollbar_geom(window, &sb_x, &sb_y, &sb_w, &sb_h, &thumb_y, &thumb_h);
        fill_rect(sb_x, sb_y, sb_w, sb_h, COLOR_FACE_ZEBRA);
        draw_frame(sb_x, sb_y, sb_w, sb_h, 1, COLOR_FACE_SHADOW);
        draw_bevel(sb_x, sb_y, sb_w, 18, COLOR_FACE, 0xFFFFFF, COLOR_FACE_SHADOW);
        draw_frame(sb_x, sb_y, sb_w, 18, 1, COLOR_INK);
        draw_text(sb_x + 4, sb_y + 1, "^", COLOR_INK);
        int32_t dn_y = sb_y + sb_h - 18;
        draw_bevel(sb_x, dn_y, sb_w, 18, COLOR_FACE, 0xFFFFFF, COLOR_FACE_SHADOW);
        draw_frame(sb_x, dn_y, sb_w, 18, 1, COLOR_INK);
        draw_text(sb_x + 4, dn_y + 1, "v", COLOR_INK);
        draw_bevel(sb_x + 1, thumb_y, sb_w - 2, thumb_h, COLOR_TITLE_STRIPE, COLOR_BEVEL_LIGHT, COLOR_BEVEL_DARK);
    }

    char count[24], total[24];
    if (has_scroll) {
        int shown_end = file_scroll + FILE_VISIBLE_ROWS;
        if (shown_end > file_count)
            shown_end = file_count;
        snprintf(count, sizeof(count), "%d-%d / %d items", file_scroll + 1, shown_end, file_count);
    } else {
        snprintf(count, sizeof(count), "%d items", file_count);
    }
    snprintf(total, sizeof(total), "%u.%u KB total", total_file_bytes / 1024, total_file_bytes % 1024 * 10 / 1024);
    int32_t status_y = y + window->height - 28;
    fill_rect(x + 4, status_y, window->width - 8, 2, COLOR_FACE_SHADOW);
    draw_text(x + 12, status_y + 6, count, COLOR_TITLE_STRIPE);
    draw_text(x + window->width - 12 - text_width(total, 1), status_y + 6, total, COLOR_TITLE_STRIPE);
}

static void draw_about_window(bool focused) {
    const struct Window *window = &windows[WINDOW_ABOUT];
    int32_t x = window->x, y = window->y;
    draw_window_chrome(window, focused);

    fill_rect(x + 20, y + 44, 96, 96, COLOR_ART_BOX);
    draw_frame(x + 20, y + 44, 96, 96, 2, COLOR_INK);
    draw_art(art_mug, x + 28, y + 54, 5);

    int32_t text_x = x + 136;
    draw_text_scaled(text_x, y + 40, "n", COLOR_TITLE, 2);
    draw_text_scaled(text_x + 16, y + 40, "OS", COLOR_AMBER_DARK, 2);
    draw_text_scaled(text_x + 48, y + 40, "leep 4.0", COLOR_TITLE, 2);
    draw_text(text_x, y + 76, "IF2130 Operating System", COLOR_TITLE_STRIPE);
    draw_text(text_x, y + 104, "x86 32-bit kernel, paging + swap", COLOR_TEXT);
    draw_text(text_x, y + 124, "round-robin scheduler @ 1000 Hz", COLOR_TEXT);
    draw_text(text_x, y + 144, "ext2 disk, VGA 800x600 desktop", COLOR_TEXT);

    char awake[48], uptime[16];
    format_duration(uptime, sizeof(uptime), sys_ticks());
    snprintf(awake, sizeof(awake), "awake for %s and counting", uptime);
    draw_text(text_x, y + 172, awake, 0x8A5A00);

    int32_t ok_x = x + window->width - 104, ok_y = y + window->height - 44;
    draw_bevel(ok_x, ok_y, 88, 28, COLOR_FACE, 0xFFFFFF, COLOR_FACE_SHADOW);
    draw_frame(ok_x, ok_y, 88, 28, 2, COLOR_INK);
    draw_text(ok_x + 44 - text_width("OK", 1) / 2, ok_y + 6, "OK", COLOR_TEXT);
}

static void draw_taskbar(void) {
    fill_rect(0, TASKBAR_TOP, SCREEN_WIDTH, SCREEN_HEIGHT - TASKBAR_TOP, COLOR_TASKBAR);
    fill_rect(0, TASKBAR_TOP, SCREEN_WIDTH, 2, COLOR_TITLE_STRIPE);

    draw_bevel(4, TASKBAR_TOP + 4, 116, 24, COLOR_TITLE, COLOR_BEVEL_LIGHT, COLOR_BEVEL_DARK);
    draw_art(art_moon, 10, TASKBAR_TOP + 8, 1);
    draw_text(32, TASKBAR_TOP + 8, "nOSleep", COLOR_AMBER);

    int slot = 0;
    for (int window = 0; window < WINDOW_COUNT; window++) {
        if (!windows[window].open)
            continue;
        int32_t x = task_button_x(slot++);
        if (window == focus)
            draw_bevel(x, TASKBAR_TOP + 4, 112, 24, COLOR_SUNKEN, COLOR_BEVEL_DARK, COLOR_BEVEL_LIGHT);
        else
            draw_bevel(x, TASKBAR_TOP + 4, 112, 24, COLOR_TITLE, COLOR_BEVEL_LIGHT, COLOR_BEVEL_DARK);
        draw_text(x + 8, TASKBAR_TOP + 8, windows[window].task_label, window == focus ? COLOR_AMBER : COLOR_LILAC);
    }

    char awake[24], clock[8], uptime[16];
    struct CMOSTime now;
    sys_time(&now);
    format_duration(uptime, sizeof(uptime), sys_ticks());
    snprintf(awake, sizeof(awake), "AWAKE %s", uptime);
    snprintf(clock, sizeof(clock), "%02d:%02d", now.hour, now.minute);

    int32_t tray_width = text_width(awake, 1) + text_width(clock, 1) + 34;
    int32_t tray_x = SCREEN_WIDTH - 4 - tray_width;
    draw_bevel(tray_x, TASKBAR_TOP + 4, tray_width, 24, COLOR_SUNKEN, COLOR_BEVEL_DARK, COLOR_BEVEL_LIGHT);
    draw_text(tray_x + 10, TASKBAR_TOP + 8, awake, COLOR_AMBER);
    draw_text(tray_x + tray_width - 10 - text_width(clock, 1), TASKBAR_TOP + 8, clock, COLOR_WHITE);
}

static void draw_screen(const struct MouseState *mouse, bool cursor_visible) {
    draw_desktop_scenery();
    draw_icons();
    for (int i = 0; i < window_stack_size; i++) {
        int window = window_stack[i];
        bool focused = window == focus;
        if (window == WINDOW_TERMINAL)
            draw_terminal_window(focused, cursor_visible);
        else if (window == WINDOW_FILES)
            draw_files_window(focused);
        else
            draw_about_window(focused);
    }
    draw_taskbar();
    draw_art(art_cursor, mouse->x, mouse->y, 1);
    sys_blit(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

static void show_splash(void) {
    uint32_t start = sys_ticks();
    while (true) {
        uint32_t elapsed = sys_ticks() - start;
        if (elapsed > SPLASH_MS)
            break;
        fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, sky_bands[0]);
        for (int i = 0; i < STAR_COUNT; i++)
            draw_rect(&stars[i]);
        draw_art(art_moon, 336, 110, 8);
        draw_logo(SCREEN_WIDTH / 2 + text_width("nOSleep", 4) / 2, 310, 4);
        draw_frame(250, 420, 300, 22, 2, COLOR_BEVEL_LIGHT);
        fill_rect(252, 422, 296, 18, COLOR_TASKBAR);
        int32_t filled = 296 * (int32_t) elapsed / SPLASH_MS;
        for (int32_t bar = 0; bar < filled; bar += 14)
            fill_rect(254 + bar, 424, (filled - bar < 12 ? filled - bar : 12), 14, COLOR_AMBER);
        const char *caption = "brewing coffee... loading desktop";
        draw_text(SCREEN_WIDTH / 2 - text_width(caption, 1) / 2, 456, caption, COLOR_LILAC_MUTED);
        sys_blit(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        sys_sleep(FRAME_MS * 2);
    }
}

static bool gui_already_running(void) {
    struct ProcessInfo list[PROCESS_LIST_MAX];
    uint32_t count = sys_ps(list, PROCESS_LIST_MAX), guis = 0;
    for (uint32_t i = 0; i < count; i++)
        guis += strcmp(list[i].name, "gui") == 0;
    return guis > 1;
}

static void run_program(const char *name) {
    uint32_t pid;
    if (sys_exec(name, ROOT_INODE, &pid) == EXEC_SUCCESS) {
        sys_wait(pid);
        while (sys_key_event() != 0)
            ;
        ignore_escape_until = sys_ticks() + ESCAPE_COOLDOWN_MS;
    }
}

static bool activate(enum Action action) {
    switch (action) {
        case ACTION_BAD_APPLE:
            run_program("badapple");
            break;
        case ACTION_FILES:
            load_files();
            raise_window(WINDOW_FILES);
            break;
        case ACTION_ABOUT:
            raise_window(WINDOW_ABOUT);
            break;
        case ACTION_TERMINAL:
            raise_window(WINDOW_TERMINAL);
            break;
    }
    return true;
}

static bool handle_key(uint16_t event) {
    if (event & KEY_EVENT_RELEASED)
        return true;
    uint8_t scancode = event & 0x7F;
    if (scancode == SCANCODE_ESCAPE && ignore_escape_until != 0 &&
        (int32_t) (sys_ticks() - ignore_escape_until) < 0)
        return true;
    if (focus == WINDOW_TERMINAL) {
        if (scancode == SCANCODE_ESCAPE)
            close_window(WINDOW_TERMINAL);
        return true;
    }
    if (focus == WINDOW_FILES) {
        if (scancode == SCANCODE_UP && file_count > 0) {
            selected_file = selected_file > 0 ? selected_file - 1 : 0;
            ensure_file_visible(selected_file);
        } else if (scancode == SCANCODE_DOWN && file_count > 0) {
            selected_file = selected_file < file_count - 1 ? selected_file + 1 : file_count - 1;
            ensure_file_visible(selected_file);
        } else if (scancode == SCANCODE_ESCAPE) {
            close_window(WINDOW_FILES);
        }
        return true;
    }
    switch (scancode) {
        case SCANCODE_UP:
            selected_icon = (selected_icon + ICON_COUNT - 1) % ICON_COUNT;
            set_focus(-1);
            break;
        case SCANCODE_DOWN:
        case SCANCODE_TAB:
            selected_icon = (selected_icon + 1) % ICON_COUNT;
            set_focus(-1);
            break;
        case SCANCODE_ENTER:
            if (focus == -1)
                return activate(icons[selected_icon].action);
            break;
        case SCANCODE_ESCAPE:
            if (focus == -1)
                return false;
            close_window(focus);
            break;
    }
    return true;
}

static void handle_files_click(int32_t mx, int32_t my) {
    if (file_count > FILE_VISIBLE_ROWS) {
        int32_t sb_x, sb_y, sb_w, sb_h, thumb_y, thumb_h;
        files_scrollbar_geom(&windows[WINDOW_FILES], &sb_x, &sb_y, &sb_w, &sb_h, &thumb_y, &thumb_h);
        if (inside(mx, my, sb_x, sb_y, sb_w, sb_h)) {
            if (my < sb_y + 18)
                file_scroll--;
            else if (my >= sb_y + sb_h - 18)
                file_scroll++;
            else if (my < thumb_y)
                file_scroll -= FILE_VISIBLE_ROWS;
            else if (my >= thumb_y + thumb_h)
                file_scroll += FILE_VISIBLE_ROWS;
            clamp_file_scroll();
            return;
        }
    }
    int row = file_row_at(mx, my);
    if (row >= 0)
        selected_file = row;
}

static void handle_press(const struct MouseState *mouse, uint32_t presses, int32_t *drag_dx, int32_t *drag_dy,
                         int *dragging, int *last_click_icon, uint32_t *last_click_time, bool *running) {
    if (mouse->y >= TASKBAR_TOP) {
        *last_click_icon = -1;
        int task = task_button_at(mouse->x, mouse->y);
        if (task >= 0) {
            if (task == WINDOW_FILES)
                load_files();
            raise_window(task);
            return;
        }
        if (inside(mouse->x, mouse->y, 4, TASKBAR_TOP + 4, 116, 24)) {
            raise_window(WINDOW_ABOUT);
            return;
        }
        return;
    }

    int window = window_at(mouse->x, mouse->y);
    if (window >= 0) {
        *last_click_icon = -1;
        struct Window *target = &windows[window];
        raise_window(window);
        if (on_close_button(target, mouse->x, mouse->y) || (window == WINDOW_ABOUT && on_ok_button(mouse->x, mouse->y))) {
            close_window(window);
        } else if (on_title_bar(target, mouse->x, mouse->y)) {
            *dragging = window;
            *drag_dx = mouse->x - target->x;
            *drag_dy = mouse->y - target->y;
        } else if (window == WINDOW_FILES) {
            handle_files_click(mouse->x, mouse->y);
        }
        return;
    }

    set_focus(-1);
    int icon = icon_at(mouse->x, mouse->y);
    uint32_t now = sys_ticks();
    if (icon < 0) {
        *last_click_icon = -1;
        return;
    }
    bool double_click = presses >= 2 || (icon == *last_click_icon && now - *last_click_time < DOUBLE_CLICK_MS);
    selected_icon = icon;
    *last_click_icon = double_click ? -1 : icon;
    *last_click_time = now;
    if (double_click)
        *running = activate(icons[icon].action);
}

int main(void) {
    if (gui_already_running()) {
        printf("gui: already running\n");
        return 0;
    }

    struct GraphicsInfo info;
    if (!sys_graphics_mode(true, &info)) {
        printf("gui: graphics mode not available\n");
        return 1;
    }
    screen = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    if (screen == NULL) {
        printf("gui: not enough memory\n");
        sys_graphics_mode(false, NULL);
        return 1;
    }
    sys_graphics_font(font);
    build_scenery();
    show_splash();
    while (sys_key_event() != 0)
        ;
    raise_window(WINDOW_TERMINAL);

    struct MouseState mouse, previous;
    struct TextScreen shown_text;
    memset(&shown_text, 0, sizeof(shown_text));
    sys_mouse(&previous);
    previous.x = -1;
    int32_t drag_dx = 0, drag_dy = 0, last_second = -1, last_click_icon = -1;
    uint32_t last_click_time = 0;
    int dragging = -1;
    bool running = true, hidden = false, cursor_visible = true;

    while (running) {
        if (!sys_graphics_owner()) {
            hidden = true;
            sys_sleep(FRAME_MS * 3);
            continue;
        }

        bool dirty = false;
        if (hidden) {
            hidden = false;
            dirty = true;
            while (sys_key_event() != 0)
                ;
            sys_mouse(&previous);
            sys_keyboard_focus(focus == WINDOW_TERMINAL);
            ignore_escape_until = sys_ticks() + ESCAPE_COOLDOWN_MS;
        }

        sys_mouse(&mouse);
        uint32_t presses = mouse.left_presses - previous.left_presses;
        if (presses > 0 || mouse.x != previous.x || mouse.y != previous.y || mouse.buttons != previous.buttons)
            dirty = true;
        if (presses > 0) {
            handle_press(&mouse, presses, &drag_dx, &drag_dy, &dragging, &last_click_icon, &last_click_time, &running);
            sys_mouse(&mouse);
        }
        if (dragging >= 0 && windows[dragging].open && (mouse.buttons & 1)) {
            int32_t nx = mouse.x - drag_dx;
            int32_t ny = mouse.y - drag_dy;
            int32_t max_x = SCREEN_WIDTH - windows[dragging].width;
            int32_t max_y = TASKBAR_TOP - TITLE_HEIGHT - 8;
            if (nx < 0)
                nx = 0;
            if (nx > max_x)
                nx = max_x;
            if (ny < 0)
                ny = 0;
            if (ny > max_y)
                ny = max_y;
            windows[dragging].x = nx;
            windows[dragging].y = ny;
        } else {
            dragging = -1;
        }

        uint16_t event;
        while (running && (event = sys_key_event()) != 0) {
            running = handle_key(event);
            sys_mouse(&mouse);
            dirty = true;
        }

        sys_text_screen(&terminal_text);
        if (memcmp(&terminal_text, &shown_text, sizeof(terminal_text)) != 0) {
            shown_text = terminal_text;
            dirty = true;
        }

        bool blink = (sys_ticks() / CURSOR_BLINK_MS) % 2 == 0;
        if (blink != cursor_visible && focus == WINDOW_TERMINAL) {
            cursor_visible = blink;
            dirty = true;
        }

        struct CMOSTime now;
        sys_time(&now);
        if (now.second != last_second) {
            last_second = now.second;
            dirty = true;
        }

        if (running && dirty)
            draw_screen(&mouse, cursor_visible);
        previous = mouse;
        sys_sleep(FRAME_MS);
    }

    free(screen);
    sys_keyboard_focus(true);
    sys_graphics_mode(false, NULL);
    return 0;
}
