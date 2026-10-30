#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define MAX_INPUT_LEN 256
#define MAX_ARGS 16
#define BLOCK_COUNT 16

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x30");
}

void sys_puts(const char* str, uint32_t color) {
    uint32_t len = strlen(str);
    syscall(6, (uint32_t)str, len, color);
}

void sys_putchar(char c, uint32_t color) {
    syscall(5, (uint32_t)c, color, 0);
}

void sys_getchar(char* c) {
    syscall(4, (uint32_t)c, 0, 0);
}

static uint32_t current_dir_inode = 2;
static char cwd_name[128] = "/";

void print_prompt(void) {
    sys_puts("OS-IF2130:", 0x0B); 
    sys_puts(cwd_name, 0x0E);     
    sys_puts("> ", 0x0F);         
}

int tokenize_input(char* input, char* argv[]) {
    int argc = 0;
    char* ptr = input;

    while (*ptr != '\0') {
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;

        argv[argc++] = ptr;
        if (argc >= MAX_ARGS) break;

        while (*ptr != ' ' && *ptr != '\0') ptr++;
        if (*ptr != '\0') {
            *ptr = '\0';
            ptr++;
        }
    }
    return argc;
}

void cmd_ls(void) {
    struct EXT2DriverRequest request = {
        .parent_inode = current_dir_inode,
    };
    int32_t retcode = -1;

    syscall(1, (uint32_t)&request, (uint32_t)&retcode, 0); 
    if (retcode != 0) {
        sys_puts("Error: Gagal membaca direktori.\n", 0x0C);
    }
}

void cmd_cd(char* target) {
    if (strcmp(target, "..") == 0) {
        sys_puts("Pindah ke parent directory..\n", 0x07);
        return;
    }

    struct EXT2DriverRequest request = {
        .name = target,
        .name_len = strlen(target),
        .parent_inode = current_dir_inode,
    };
    int32_t retcode = -1;

    syscall(1, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode == 0) {
        strcpy(cwd_name, target);
    } else {
        sys_puts("cd: Tidak ada direktori bernama ", 0x0C);
        sys_puts(target, 0x0C);
        sys_puts("\n", 0x0C);
    }
}

void cmd_mkdir(char* foldername) {
    struct EXT2DriverRequest request = {
        .name = foldername,
        .name_len = strlen(foldername),
        .parent_inode = current_dir_inode,
        .buffer_size = 0,
    };
    int32_t retcode = -1;

    syscall(2, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode != 0) {
        sys_puts("mkdir: Gagal membuat folder baru.\n", 0x0C);
    }
}

void cmd_cat(char* filename) {
    uint8_t buffer[1024] = {0};
    struct EXT2DriverRequest request = {
        .buf = buffer,
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = current_dir_inode,
        .buffer_size = 1024,
    };
    int32_t retcode = -1;

    syscall(0, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode == 0) {
        sys_puts((char*)buffer, 0x0F);
        sys_puts("\n", 0x0F);
    } else {
        sys_puts("cat: File tidak ditemukan.\n", 0x0C);
    }
}

void cmd_cp(char* src, char* dest) {
    uint8_t buffer[2048] = {0};
    struct EXT2DriverRequest read_req = {
        .buf = buffer,
        .name = src,
        .name_len = strlen(src),
        .parent_inode = current_dir_inode,
        .buffer_size = 2048,
    };
    int32_t retcode = -1;

    syscall(0, (uint32_t)&read_req, (uint32_t)&retcode, 0);
    if (retcode == 0) {
        struct EXT2DriverRequest write_req = {
            .buf = buffer,
            .name = dest,
            .name_len = strlen(dest),
            .parent_inode = current_dir_inode,
            .buffer_size = read_req.buffer_size,
        };
        syscall(2, (uint32_t)&write_req, (uint32_t)&retcode, 0);
        if (retcode != 0) sys_puts("cp: Gagal menulis file target.\n", 0x0C);
    } else {
        sys_puts("cp: File sumber tidak ditemukan.\n", 0x0C);
    }
}

void cmd_rm(char* filename) {
    struct EXT2DriverRequest request = {
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = current_dir_inode,
    };
    int32_t retcode = -1;

    syscall(3, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode != 0) {
        sys_puts("rm: Gagal menghapus file.\n", 0x0C);
    }
}

void cmd_mv(char* src, char* dest) {
    cmd_cp(src, dest);
    cmd_rm(src);
}

void cmd_grep(char* pattern, char* filename) {
    uint8_t buffer[2048] = {0};
    struct EXT2DriverRequest read_req = {
        .buf = buffer,
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = current_dir_inode,
        .buffer_size = 2048,
    };
    int32_t retcode = -1;

    syscall(0, (uint32_t)&read_req, (uint32_t)&retcode, 0);
    if (retcode == 0) {
        char* line = (char*)buffer;
        while (*line != '\0') {
            if (strstr(line, pattern) != NULL) {
                sys_puts(line, 0x0A); 
                sys_puts("\n", 0x0F);
            }
            while (*line != '\n' && *line != '\0') line++;
            if (*line == '\n') line++;
        }
    } else {
        sys_puts("grep: File tidak ditemukan.\n", 0x0C);
    }
}

int main(void) {
    syscall(7, 0, 0, 0);

    char input_buf[MAX_INPUT_LEN];
    int input_idx = 0;

    sys_puts("Selamat datang di IF2130 Shell!\n", 0x0A);

    while (true) {
        print_prompt();

        input_idx = 0;
        memset(input_buf, 0, MAX_INPUT_LEN);

        while (true) {
            char c = 0;
            sys_getchar(&c);

            if (c == '\n') {
                sys_putchar('\n', 0x0F);
                break;
            } else if (c == '\b') {
                if (input_idx > 0) {
                    input_idx--;
                    input_buf[input_idx] = '\0';
                    sys_putchar('\b', 0x0F);
                }
            } else if (c >= 32 && c <= 126) {
                if (input_idx < MAX_INPUT_LEN - 1) {
                    input_buf[input_idx++] = c;
                    sys_putchar(c, 0x0F);
                }
            }
        }

        if (input_idx == 0) continue;

        char* argv[MAX_ARGS] = {0};
        int argc = tokenize_input(input_buf, argv);

        if (argc == 0) continue;

        if (strcmp(argv[0], "cd") == 0) {
            if (argc > 1) cmd_cd(argv[1]);
            else sys_puts("Penggunaan: cd <directory>\n", 0x0E);
        } 
        else if (strcmp(argv[0], "ls") == 0) {
            cmd_ls();
        } 
        else if (strcmp(argv[0], "mkdir") == 0) {
            if (argc > 1) cmd_mkdir(argv[1]);
            else sys_puts("Penggunaan: mkdir <foldername>\n", 0x0E);
        } 
        else if (strcmp(argv[0], "cat") == 0) {
            if (argc > 1) cmd_cat(argv[1]);
            else sys_puts("Penggunaan: cat <filename>\n", 0x0E);
        } 
        else if (strcmp(argv[0], "cp") == 0) {
            if (argc > 2) cmd_cp(argv[1], argv[2]);
            else sys_puts("Penggunaan: cp <src> <dest>\n", 0x0E);
        } 
        else if (strcmp(argv[0], "rm") == 0) {
            if (argc > 1) cmd_rm(argv[1]);
            else sys_puts("Penggunaan: rm <filename>\n", 0x0E);
        } 
        else if (strcmp(argv[0], "mv") == 0) {
            if (argc > 2) cmd_mv(argv[1], argv[2]);
            else sys_puts("Penggunaan: mv <src> <dest>\n", 0x0E);
        } 
        else if (strcmp(argv[0], "grep") == 0) {
            if (argc > 2) cmd_grep(argv[1], argv[2]);
            else sys_puts("Penggunaan: grep <pattern> <filename>\n", 0x0E);
        } 
        else {
            sys_puts("Perintah tidak dikenal: ", 0x0C);
            sys_puts(argv[0], 0x0C);
            sys_puts("\n", 0x0C);
        }
    }

    return 0;
}