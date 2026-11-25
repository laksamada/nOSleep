#ifndef _NOSLEEP_H
#define _NOSLEEP_H

#include <stdint.h>
#include <stdbool.h>
#include "header/process/syscall.h"
#include "header/filesystem/ext2.h"

#define EXEC_SUCCESS 0

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

int32_t  sys_exec(const char *name, uint32_t parent_inode, uint32_t *pid);
bool     sys_process_alive(uint32_t pid);
void     sys_wait(uint32_t pid);
void     sys_yield(void);
void     sys_exit(int code) __attribute__((noreturn));
uint32_t sys_ticks(void);
void     sys_sleep(uint32_t ms);
void     sys_time(struct CMOSTime *time);
void     sys_write_at(const char *text, uint8_t row, uint8_t col, uint8_t color);
void     sys_puts(const char *text, uint32_t length, uint32_t color);

bool     sys_graphics_mode(bool enable, struct GraphicsInfo *info);
void     sys_blit(const uint32_t *pixels, int32_t x, int32_t y, uint32_t width, uint32_t height);
void     sys_mouse(struct MouseState *state);
uint16_t sys_key_event(void);
void     sys_graphics_font(uint8_t *font);
void     sys_text_screen(struct TextScreen *screen);
void     sys_keyboard_focus(bool focused);
bool     sys_graphics_owner(void);
uint32_t sys_ps(struct ProcessInfo *list, uint32_t max_count);

int8_t   sys_read_file(const char *name, uint32_t parent_inode, void *buf, uint32_t size);
int8_t   sys_read_directory(uint32_t inode, void *buf, uint32_t size);
uint32_t sys_inode_size(uint32_t inode);

#endif
