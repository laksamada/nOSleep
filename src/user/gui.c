#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nosleep.h>

#define SCREEN_WIDTH    800
#define SCREEN_HEIGHT   600
#define TASKBAR_HEIGHT  30
#define ICON_SIZE       48
#define ICON_CELL       90
#define ICON_MARGIN     28
#define TITLE_HEIGHT    22
#define CLOSE_SIZE      16
#define LINE_HEIGHT     18
#define CHAR_WIDTH      8
#define DOUBLE_CLICK_MS 400
#define FRAME_MS        16
#define FILE_LINES_MAX  14
#define ROOT_INODE      1

#define SCANCODE_ESCAPE 0x01
#define SCANCODE_TAB    0x0F
#define SCANCODE_ENTER  0x1C
#define SCANCODE_UP     0x48
#define SCANCODE_DOWN   0x50

#define COLOR_DESKTOP   0x1D4E5F
#define COLOR_TASKBAR   0x12202A
#define COLOR_WINDOW    0xF2F2F2
#define COLOR_TITLE     0x2C6E91
#define COLOR_BORDER    0x0B1A22
#define COLOR_TEXT      0x1A1A1A
#define COLOR_MUTED     0x9FB4BF
#define COLOR_WHITE     0xFFFFFF
#define COLOR_BLACK     0x000000
#define COLOR_SELECTED  0x3F7F96
#define COLOR_CLOSE     0xC0392B

enum Action {
    ACTION_DOOM,
    ACTION_FILES,
    ACTION_ABOUT,
    ACTION_SHELL,
};

struct Icon {
    const char *label;
    enum Action action;
};

static const struct Icon icons[] = {
    {"Doom",    ACTION_DOOM},
    {"Files",   ACTION_FILES},
    {"About",   ACTION_ABOUT},
    {"Shell",   ACTION_SHELL},
};

#define ICON_COUNT ((int) (sizeof(icons) / sizeof(icons[0])))

static const char *about_lines[] = {
    "nOSleep - IF2130 Operating System",
    "",
    "Mouse   : double-click an icon to open it,",
    "          drag a window title to move it",
    "Keyboard: arrows/Tab select, Enter opens,",
    "          Esc closes the window or exits",
    "",
    "Doom    : arrows move, Ctrl fires,",
    "          Space opens doors, Esc menu",
};

static const char *cursor_shape[] = {
    "X",
    "XX",
    "X.X",
    "X..X",
    "X...X",
    "X....X",
    "X.....X",
    "X......X",
    "X.......X",
    "X........X",
    "X......XXXX",
    "X...X..X",
    "X..XX..X",
    "X.X  X..X",
    "XX   X..X",
    "      XX",
};

struct Window {
    bool        open;
    enum Action content;
    int32_t     x, y, width, height;
};

struct FileLine {
    char name[32];
    char size[16];
};

static uint32_t *screen;
static uint8_t font[GRAPHICS_FONT_SIZE];
static struct Window window;
static struct FileLine files[FILE_LINES_MAX];
static int file_count;
static int selected_icon;
static char status_text[64] = "Double-click an icon to open it";

static void fill_rect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + width > SCREEN_WIDTH ? SCREEN_WIDTH : x + width;
    int32_t y1 = y + height > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + height;
    for (int32_t row = y0; row < y1; row++)
        for (int32_t col = x0; col < x1; col++)
            screen[row * SCREEN_WIDTH + col] = color;
}

static void draw_border(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    fill_rect(x, y, width, 1, color);
    fill_rect(x, y + height - 1, width, 1, color);
    fill_rect(x, y, 1, height, color);
    fill_rect(x + width - 1, y, 1, height, color);
}

static void draw_char(int32_t x, int32_t y, char c, uint32_t color) {
    const uint8_t *glyph = &font[(uint8_t) c * GRAPHICS_FONT_HEIGHT];
    for (int32_t row = 0; row < GRAPHICS_FONT_HEIGHT; row++)
        for (int32_t col = 0; col < CHAR_WIDTH; col++)
            if (glyph[row] & (0x80 >> col))
                fill_rect(x + col, y + row, 1, 1, color);
}

static void draw_text(int32_t x, int32_t y, const char *text, uint32_t color) {
    for (; *text != '\0'; text++, x += CHAR_WIDTH)
        draw_char(x, y, *text, color);
}

static int32_t text_width(const char *text) {
    return strlen(text) * CHAR_WIDTH;
}

static bool inside(int32_t px, int32_t py, int32_t x, int32_t y, int32_t width, int32_t height) {
    return px >= x && px < x + width && py >= y && py < y + height;
}

static int32_t icon_x(void) {
    return ICON_MARGIN;
}

static int32_t icon_y(int index) {
    return ICON_MARGIN + index * ICON_CELL;
}

static int icon_at(int32_t x, int32_t y) {
    for (int i = 0; i < ICON_COUNT; i++)
        if (inside(x, y, icon_x() - 14, icon_y(i) - 4, ICON_SIZE + 28, ICON_SIZE + 28))
            return i;
    return -1;
}

static void draw_icon_picture(enum Action action, int32_t x, int32_t y) {
    switch (action) {
        case ACTION_DOOM:
            fill_rect(x, y, ICON_SIZE, ICON_SIZE, 0x5A0A0A);
            draw_border(x, y, ICON_SIZE, ICON_SIZE, 0xD35400);
            draw_text(x + 8, y + 16, "DOOM", 0xF39C12);
            break;
        case ACTION_FILES:
            fill_rect(x, y + 6, 20, 8, 0xD4A12A);
            fill_rect(x, y + 12, ICON_SIZE, 34, 0xF4C542);
            draw_border(x, y + 12, ICON_SIZE, 34, 0x9A7B1C);
            break;
        case ACTION_ABOUT:
            fill_rect(x, y, ICON_SIZE, ICON_SIZE, 0x2E86DE);
            draw_border(x, y, ICON_SIZE, ICON_SIZE, COLOR_WHITE);
            draw_text(x + 20, y + 16, "i", COLOR_WHITE);
            break;
        case ACTION_SHELL:
            fill_rect(x, y, ICON_SIZE, ICON_SIZE, COLOR_BLACK);
            draw_border(x, y, ICON_SIZE, ICON_SIZE, COLOR_MUTED);
            draw_text(x + 6, y + 6, ">_", 0x2ECC71);
            break;
    }
}

static void draw_icons(void) {
    for (int i = 0; i < ICON_COUNT; i++) {
        if (i == selected_icon)
            fill_rect(icon_x() - 14, icon_y(i) - 4, ICON_SIZE + 28, ICON_SIZE + 28, COLOR_SELECTED);
        draw_icon_picture(icons[i].action, icon_x(), icon_y(i));
        const char *label = icons[i].label;
        draw_text(icon_x() + ICON_SIZE / 2 - text_width(label) / 2, icon_y(i) + ICON_SIZE + 4, label, COLOR_WHITE);
    }
}

static void load_files(void) {
    uint8_t block[BLOCK_SIZE];
    file_count = 0;
    if (sys_read_directory(ROOT_INODE, block) != 0)
        return;

    uint32_t offset = 0;
    while (offset < BLOCK_SIZE && file_count < FILE_LINES_MAX) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (block + offset);
        if (entry->inode == 0 || entry->rec_len == 0)
            break;
        offset += entry->rec_len;

        const char *name = (const char *) (entry + 1);
        bool dot = (entry->name_len == 1 && name[0] == '.') ||
                   (entry->name_len == 2 && name[0] == '.' && name[1] == '.');
        if (dot)
            continue;

        struct FileLine *line = &files[file_count++];
        int len = entry->name_len < sizeof(line->name) - 1 ? entry->name_len : sizeof(line->name) - 1;
        memcpy(line->name, name, len);
        line->name[len] = '\0';
        if (entry->file_type == EXT2_FT_DIR)
            strcpy(line->size, "<folder>");
        else
            snprintf(line->size, sizeof(line->size), "%u B", sys_inode_size(entry->inode));
    }
}

static void open_window(enum Action content) {
    window.open    = true;
    window.content = content;
    window.width   = content == ACTION_FILES ? 380 : 440;
    window.height  = content == ACTION_FILES ? 320 : 220;
    window.x       = (SCREEN_WIDTH - window.width) / 2;
    window.y       = (SCREEN_HEIGHT - TASKBAR_HEIGHT - window.height) / 2;
}

static bool on_close_button(int32_t x, int32_t y) {
    return inside(x, y, window.x + window.width - CLOSE_SIZE - 4, window.y + 3, CLOSE_SIZE, CLOSE_SIZE);
}

static bool on_title_bar(int32_t x, int32_t y) {
    return inside(x, y, window.x, window.y, window.width, TITLE_HEIGHT);
}

static void draw_window(void) {
    int32_t x = window.x, y = window.y;
    fill_rect(x + 4, y + 4, window.width, window.height, 0x0F2A33);
    fill_rect(x, y, window.width, window.height, COLOR_WINDOW);
    fill_rect(x, y, window.width, TITLE_HEIGHT, COLOR_TITLE);
    draw_border(x, y, window.width, window.height, COLOR_BORDER);
    draw_text(x + 8, y + 3, window.content == ACTION_FILES ? "Files in /" : "About nOSleep", COLOR_WHITE);

    int32_t close_x = x + window.width - CLOSE_SIZE - 4;
    fill_rect(close_x, y + 3, CLOSE_SIZE, CLOSE_SIZE, COLOR_CLOSE);
    draw_text(close_x + 4, y + 3, "x", COLOR_WHITE);

    int32_t text_x = x + 12, text_y = y + TITLE_HEIGHT + 10;
    if (window.content == ACTION_ABOUT) {
        for (uint32_t i = 0; i < sizeof(about_lines) / sizeof(about_lines[0]); i++)
            draw_text(text_x, text_y + i * LINE_HEIGHT, about_lines[i], COLOR_TEXT);
        return;
    }

    draw_text(text_x, text_y, "Name", COLOR_TITLE);
    draw_text(text_x + 220, text_y, "Size", COLOR_TITLE);
    for (int i = 0; i < file_count; i++) {
        draw_text(text_x, text_y + (i + 1) * LINE_HEIGHT, files[i].name, COLOR_TEXT);
        draw_text(text_x + 220, text_y + (i + 1) * LINE_HEIGHT, files[i].size, COLOR_TEXT);
    }
}

static void draw_taskbar(void) {
    int32_t y = SCREEN_HEIGHT - TASKBAR_HEIGHT;
    fill_rect(0, y, SCREEN_WIDTH, TASKBAR_HEIGHT, COLOR_TASKBAR);
    fill_rect(0, y, SCREEN_WIDTH, 1, COLOR_SELECTED);
    draw_text(12, y + 7, "nOSleep", COLOR_WHITE);
    draw_text(13, y + 7, "nOSleep", COLOR_WHITE);
    draw_text(120, y + 7, status_text, COLOR_MUTED);

    struct CMOSTime now;
    char clock[16];
    sys_time(&now);
    snprintf(clock, sizeof(clock), "%02d:%02d:%02d", now.hour, now.minute, now.second);
    draw_text(SCREEN_WIDTH - text_width(clock) - 12, y + 7, clock, COLOR_WHITE);
}

static void draw_cursor(int32_t x, int32_t y) {
    for (uint32_t row = 0; row < sizeof(cursor_shape) / sizeof(cursor_shape[0]); row++)
        for (int32_t col = 0; cursor_shape[row][col] != '\0'; col++) {
            char pixel = cursor_shape[row][col];
            if (pixel != ' ')
                fill_rect(x + col, y + row, 1, 1, pixel == 'X' ? COLOR_BLACK : COLOR_WHITE);
        }
}

static void draw_screen(const struct MouseState *mouse) {
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - TASKBAR_HEIGHT, COLOR_DESKTOP);
    draw_icons();
    if (window.open)
        draw_window();
    draw_taskbar();
    draw_cursor(mouse->x, mouse->y);
    sys_blit(screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

static void run_program(const char *name) {
    uint32_t pid;
    if (sys_exec(name, ROOT_INODE, &pid) != EXEC_SUCCESS) {
        snprintf(status_text, sizeof(status_text), "Failed to run %s", name);
        return;
    }
    sys_wait(pid);
    while (sys_key_event() != 0)
        ;
    snprintf(status_text, sizeof(status_text), "%s exited", name);
}

static bool activate(enum Action action) {
    switch (action) {
        case ACTION_DOOM:
            run_program("doom");
            break;
        case ACTION_FILES:
            load_files();
            open_window(ACTION_FILES);
            break;
        case ACTION_ABOUT:
            open_window(ACTION_ABOUT);
            break;
        case ACTION_SHELL:
            return false;
    }
    return true;
}

static bool handle_key(uint16_t event) {
    if (event & KEY_EVENT_RELEASED)
        return true;
    switch (event & 0x7F) {
        case SCANCODE_UP:
            selected_icon = (selected_icon + ICON_COUNT - 1) % ICON_COUNT;
            break;
        case SCANCODE_DOWN:
        case SCANCODE_TAB:
            selected_icon = (selected_icon + 1) % ICON_COUNT;
            break;
        case SCANCODE_ENTER:
            return activate(icons[selected_icon].action);
        case SCANCODE_ESCAPE:
            if (!window.open)
                return false;
            window.open = false;
            break;
    }
    return true;
}

int main(void) {
    struct GraphicsInfo info;
    if (!sys_graphics_mode(true, &info)) {
        printf("gui: graphics mode not available\n");
        return 1;
    }
    screen = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    sys_graphics_font(font);
    while (sys_key_event() != 0)
        ;

    struct MouseState mouse, previous = {.x = -1};
    int32_t drag_dx = 0, drag_dy = 0, last_second = -1, last_click_icon = -1;
    uint32_t last_click_time = 0;
    bool dragging = false, running = true;

    while (running) {
        bool dirty = false;
        sys_mouse(&mouse);
        bool pressed  = (mouse.buttons & 1) && !(previous.buttons & 1);
        bool released = !(mouse.buttons & 1) && (previous.buttons & 1);
        if (mouse.x != previous.x || mouse.y != previous.y || mouse.buttons != previous.buttons)
            dirty = true;

        if (pressed && window.open && inside(mouse.x, mouse.y, window.x, window.y, window.width, window.height)) {
            if (on_close_button(mouse.x, mouse.y)) {
                window.open = false;
            } else if (on_title_bar(mouse.x, mouse.y)) {
                dragging = true;
                drag_dx = mouse.x - window.x;
                drag_dy = mouse.y - window.y;
            }
        } else if (pressed) {
            int icon = icon_at(mouse.x, mouse.y);
            uint32_t now = sys_ticks();
            if (icon >= 0) {
                bool double_click = icon == last_click_icon && now - last_click_time < DOUBLE_CLICK_MS;
                selected_icon = icon;
                last_click_icon = double_click ? -1 : icon;
                last_click_time = now;
                if (double_click)
                    running = activate(icons[icon].action);
            }
        }
        if (dragging && (mouse.buttons & 1)) {
            window.x = mouse.x - drag_dx;
            window.y = mouse.y - drag_dy;
        }
        if (released)
            dragging = false;

        uint16_t event;
        while (running && (event = sys_key_event()) != 0) {
            running = handle_key(event);
            dirty = true;
        }

        struct CMOSTime now;
        sys_time(&now);
        if (now.second != last_second) {
            last_second = now.second;
            dirty = true;
        }

        if (running && dirty)
            draw_screen(&mouse);
        previous = mouse;
        sys_sleep(FRAME_MS);
    }

    free(screen);
    sys_graphics_mode(false, NULL);
    return 0;
}
