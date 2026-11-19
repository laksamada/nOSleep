#include <nosleep.h>
#include <string.h>

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : "+a"(eax), "+b"(ebx), "+c"(ecx), "+d"(edx) : /* <Empty> */ : "memory");
}

int32_t sys_exec(const char *name, uint32_t parent_inode, uint32_t *pid) {
    struct EXT2DriverRequest request = {
        .buf          = (void *) 0,
        .name         = (char *) name,
        .name_len     = strlen(name),
        .parent_inode = parent_inode,
        .buffer_size  = 0x200000,
    };
    int32_t retcode = -1;
    syscall(SYSCALL_EXEC, (uint32_t) &request, (uint32_t) &retcode, (uint32_t) pid);
    return retcode;
}

bool sys_process_alive(uint32_t pid) {
    bool alive = false;
    syscall(SYSCALL_PROCESS_ALIVE, pid, (uint32_t) &alive, 0);
    return alive;
}

void sys_wait(uint32_t pid) {
    while (sys_process_alive(pid))
        sys_yield();
}

void sys_yield(void) {
    syscall(SYSCALL_YIELD, 0, 0, 0);
}

void sys_exit(int code) {
    syscall(SYSCALL_EXIT, code, 0, 0);
    while (true)
        ;
}

uint32_t sys_ticks(void) {
    uint32_t ticks = 0;
    syscall(SYSCALL_GET_TICKS, (uint32_t) &ticks, 0, 0);
    return ticks;
}

void sys_sleep(uint32_t ms) {
    uint32_t start = sys_ticks();
    while (sys_ticks() - start < ms)
        sys_yield();
}

void sys_time(struct CMOSTime *time) {
    syscall(SYSCALL_GET_TIME, (uint32_t) time, 0, 0);
}

void sys_write_at(const char *text, uint8_t row, uint8_t col, uint8_t color) {
    syscall(SYSCALL_WRITE_AT, (uint32_t) text, (row << 8) | col, color);
}

void sys_puts(const char *text, uint32_t length, uint32_t color) {
    syscall(SYSCALL_PUTS, (uint32_t) text, length, color);
}

bool sys_graphics_mode(bool enable, struct GraphicsInfo *info) {
    struct GraphicsInfo result = {0};
    syscall(SYSCALL_GRAPHICS_MODE, enable, (uint32_t) &result, 0);
    if (info != NULL)
        *info = result;
    return result.width != 0;
}

void sys_blit(const uint32_t *pixels, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    struct BlitRequest request = {
        .pixels = pixels,
        .x      = x,
        .y      = y,
        .width  = width,
        .height = height,
    };
    syscall(SYSCALL_GRAPHICS_BLIT, (uint32_t) &request, 0, 0);
}

void sys_mouse(struct MouseState *state) {
    syscall(SYSCALL_MOUSE_STATE, (uint32_t) state, 0, 0);
}

uint16_t sys_key_event(void) {
    uint16_t event = 0;
    syscall(SYSCALL_KEY_EVENT, (uint32_t) &event, 0, 0);
    return event;
}

int32_t sys_boot_file_size(const char *name) {
    int32_t size = -1;
    syscall(SYSCALL_BOOT_FILE_SIZE, (uint32_t) name, (uint32_t) &size, 0);
    return size;
}

uint32_t sys_boot_file_read(const char *name, uint32_t offset, void *buf, uint32_t size) {
    struct BootFileRead request = {
        .name   = name,
        .offset = offset,
        .buf    = buf,
        .size   = size,
    };
    uint32_t bytes_read = 0;
    syscall(SYSCALL_BOOT_FILE_READ, (uint32_t) &request, (uint32_t) &bytes_read, 0);
    return bytes_read;
}

int8_t sys_read_directory(uint32_t inode, void *block) {
    struct EXT2DriverRequest request = {
        .buf          = block,
        .parent_inode = inode,
        .buffer_size  = BLOCK_SIZE,
    };
    int8_t retcode = -1;
    syscall(SYSCALL_READ_DIRECTORY, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode;
}

uint32_t sys_inode_size(uint32_t inode) {
    uint32_t size = 0;
    syscall(SYSCALL_FILE_SIZE, inode, (uint32_t) &size, 0);
    return size;
}

void sys_graphics_font(uint8_t *font) {
    syscall(SYSCALL_GRAPHICS_FONT, (uint32_t) font, 0, 0);
}
