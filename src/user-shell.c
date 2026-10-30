#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define MAX_INPUT_LEN 256
#define MAX_ARGS 16
#define MAX_PIPELINE_STAGES 8
#define MAX_PATH_LEN 256
#define MAX_NAME_LEN 256
#define ROOT_INODE 1

#define IO_BUFFER_SIZE 0x40000u
#define FILE_BUFFER    ((uint8_t *) 0x100000)
#define PIPE_BUFFER_A  ((char *) 0x140000)
#define PIPE_BUFFER_B  ((char *) 0x180000)

#define MEMTEST_BASE      0x200000u
#define MEMTEST_MAX_PAGES 496u
#define MEMTEST_PAGE_SIZE 0x1000u

#define COLOR_OUTPUT 0x0F
#define COLOR_ERROR  0x0C
#define COLOR_USAGE  0x0E
#define COLOR_PROMPT 0x0B
#define COLOR_WELCOME 0x0A

static uint32_t current_dir_inode = ROOT_INODE;
static char cwd_name[MAX_PATH_LEN] = "/";

static char *output_buffer;
static uint32_t output_len;
static const char *input_data;
static uint32_t input_len;
static bool has_input;

static char find_path[MAX_PATH_LEN];
static uint32_t find_count;

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : /* <Empty> */ : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : /* <Empty> */ : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : /* <Empty> */ : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : /* <Empty> */ : "r"(eax));
    // Note : gcc usually use %eax as intermediate register,
    //        so it need to be the last one to mov
    __asm__ volatile("int $0x30");
}

static uint32_t strlen(const char *str) {
    uint32_t len = 0;
    while (str[len] != '\0')
        len++;
    return len;
}

static int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t) *a - (uint8_t) *b;
}

static void strcpy(char *dest, const char *src) {
    while (*src != '\0')
        *dest++ = *src++;
    *dest = '\0';
}

static void sys_puts(const char *str, uint32_t color) {
    syscall(6, (uint32_t) str, strlen(str), color);
}

static void sys_write_buffer(const char *buf, uint32_t len, uint32_t color) {
    syscall(6, (uint32_t) buf, len, color);
}

static void sys_putchar(char c, uint32_t color) {
    syscall(5, (uint32_t) c, color, 0);
}

static void sys_getchar(char *c) {
    syscall(4, (uint32_t) c, 0, 0);
}

static void print_error(const char *prefix, const char *message, const char *name) {
    sys_puts(prefix, COLOR_ERROR);
    sys_puts(": ", COLOR_ERROR);
    sys_puts(message, COLOR_ERROR);
    if (name != NULL) {
        sys_puts(" ", COLOR_ERROR);
        sys_puts(name, COLOR_ERROR);
    }
    sys_puts("\n", COLOR_ERROR);
}

static void print_usage(const char *usage) {
    sys_puts("Penggunaan: ", COLOR_USAGE);
    sys_puts(usage, COLOR_USAGE);
    sys_puts("\n", COLOR_USAGE);
}

static void out_char(char c) {
    if (output_len < IO_BUFFER_SIZE)
        output_buffer[output_len++] = c;
}

static void out_buffer(const char *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        out_char(buf[i]);
}

static void out_str(const char *str) {
    out_buffer(str, strlen(str));
}

static void out_uint(uint32_t value) {
    char digits[10];
    int count = 0;
    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value != 0);
    while (count > 0)
        out_char(digits[--count]);
}

static void print_prompt(void) {
    sys_puts("OS-IF2130:", COLOR_PROMPT);
    sys_puts(cwd_name, COLOR_USAGE);
    sys_puts("> ", COLOR_OUTPUT);
}

static bool read_directory_block(uint32_t inode, uint8_t *block) {
    struct EXT2DriverRequest request = {
        .buf          = block,
        .name         = NULL,
        .name_len     = 0,
        .parent_inode = inode,
        .buffer_size  = BLOCK_SIZE,
    };
    int8_t retcode = -1;
    syscall(1, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode == 0;
}

static struct EXT2DirectoryEntry *next_entry(uint8_t *block, uint32_t *offset) {
    if (*offset >= BLOCK_SIZE)
        return NULL;
    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (block + *offset);
    if (entry->inode == 0 || entry->rec_len == 0)
        return NULL;
    *offset += entry->rec_len;
    return entry;
}

static const char *entry_name(struct EXT2DirectoryEntry *entry) {
    return (const char *) (entry + 1);
}

static bool entry_name_equal(struct EXT2DirectoryEntry *entry, const char *name) {
    uint32_t len = strlen(name);
    return entry->name_len == len && memcmp(entry_name(entry), name, len) == 0;
}

static bool is_dot_entry(struct EXT2DirectoryEntry *entry) {
    return entry_name_equal(entry, ".") || entry_name_equal(entry, "..");
}

static void copy_entry_name(struct EXT2DirectoryEntry *entry, char *dest) {
    memcpy(dest, entry_name(entry), entry->name_len);
    dest[entry->name_len] = '\0';
}

static bool find_entry(uint32_t parent, const char *name, uint32_t *inode, uint8_t *file_type) {
    uint8_t block[BLOCK_SIZE];
    if (!read_directory_block(parent, block))
        return false;

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    while ((entry = next_entry(block, &offset)) != NULL) {
        if (entry_name_equal(entry, name)) {
            *inode = entry->inode;
            *file_type = entry->file_type;
            return true;
        }
    }
    return false;
}

static int8_t write_entry(uint32_t parent, const char *name, void *buf, uint32_t size, bool is_directory) {
    struct EXT2DriverRequest request = {
        .buf          = buf,
        .name         = (char *) name,
        .name_len     = strlen(name),
        .parent_inode = parent,
        .buffer_size  = size,
        .is_directory = is_directory,
    };
    int8_t retcode = -1;
    syscall(2, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t delete_entry(uint32_t parent, const char *name, bool is_directory) {
    struct EXT2DriverRequest request = {
        .name         = (char *) name,
        .name_len     = strlen(name),
        .parent_inode = parent,
        .is_directory = is_directory,
    };
    int8_t retcode = -1;
    syscall(3, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t load_file(uint32_t parent, const char *name, uint32_t *size) {
    uint32_t inode;
    uint8_t file_type;
    if (!find_entry(parent, name, &inode, &file_type))
        return 3;
    if (file_type == EXT2_FT_DIR)
        return 1;

    syscall(9, inode, (uint32_t) size, 0);
    if (*size > IO_BUFFER_SIZE)
        return 2;

    struct EXT2DriverRequest request = {
        .buf          = FILE_BUFFER,
        .name         = (char *) name,
        .name_len     = strlen(name),
        .parent_inode = parent,
        .buffer_size  = IO_BUFFER_SIZE,
    };
    int8_t retcode = -1;
    syscall(0, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode;
}

static bool copy_tree(uint32_t src_parent, const char *src_name, uint32_t dest_parent, const char *dest_name) {
    uint32_t inode;
    uint8_t file_type;
    if (!find_entry(src_parent, src_name, &inode, &file_type))
        return false;

    if (file_type != EXT2_FT_DIR) {
        uint32_t size = 0;
        if (load_file(src_parent, src_name, &size) != 0)
            return false;
        return write_entry(dest_parent, dest_name, FILE_BUFFER, size, false) == 0;
    }

    if (write_entry(dest_parent, dest_name, NULL, 0, true) != 0)
        return false;

    uint32_t dest_inode;
    if (!find_entry(dest_parent, dest_name, &dest_inode, &file_type))
        return false;

    uint8_t block[BLOCK_SIZE];
    if (!read_directory_block(inode, block))
        return false;

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    while ((entry = next_entry(block, &offset)) != NULL) {
        if (is_dot_entry(entry))
            continue;
        char child[MAX_NAME_LEN];
        copy_entry_name(entry, child);
        if (!copy_tree(inode, child, dest_inode, child))
            return false;
    }
    return true;
}

static bool remove_tree(uint32_t parent, const char *name) {
    uint32_t inode;
    uint8_t file_type;
    if (!find_entry(parent, name, &inode, &file_type))
        return false;
    if (file_type != EXT2_FT_DIR)
        return delete_entry(parent, name, false) == 0;

    uint8_t block[BLOCK_SIZE];
    if (!read_directory_block(inode, block))
        return false;

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    while ((entry = next_entry(block, &offset)) != NULL) {
        if (is_dot_entry(entry))
            continue;
        char child[MAX_NAME_LEN];
        copy_entry_name(entry, child);
        if (!remove_tree(inode, child))
            return false;
    }
    return delete_entry(parent, name, true) == 0;
}

static bool resolve_destination(const char *src, const char *dest, uint32_t *dest_parent, const char **dest_name) {
    uint32_t inode;
    uint8_t file_type;
    *dest_parent = current_dir_inode;
    *dest_name = dest;

    if (strcmp(dest, ".") == 0) {
        *dest_name = src;
    } else if (strcmp(dest, "..") == 0) {
        if (!find_entry(current_dir_inode, "..", dest_parent, &file_type))
            return false;
        *dest_name = src;
    } else if (find_entry(current_dir_inode, dest, &inode, &file_type) && file_type == EXT2_FT_DIR) {
        *dest_parent = inode;
        *dest_name = src;
    }
    return true;
}

static bool prepare_destination(const char *command, const char *src, uint32_t dest_parent, const char *dest_name) {
    uint32_t src_inode, dest_inode;
    uint8_t src_type, dest_type;

    if (!find_entry(current_dir_inode, src, &src_inode, &src_type)) {
        print_error(command, "tidak ditemukan:", src);
        return false;
    }
    if (src_inode == dest_parent) {
        print_error(command, "tidak bisa ke dalam dirinya sendiri:", src);
        return false;
    }
    if (!find_entry(dest_parent, dest_name, &dest_inode, &dest_type))
        return true;

    if (dest_inode == src_inode) {
        print_error(command, "sumber dan tujuan sama:", src);
        return false;
    }
    if (dest_type == EXT2_FT_DIR) {
        print_error(command, "tujuan sudah ada dan berupa folder:", dest_name);
        return false;
    }
    if (delete_entry(dest_parent, dest_name, false) != 0) {
        print_error(command, "gagal menimpa", dest_name);
        return false;
    }
    return true;
}

static bool cmd_ls(void) {
    uint8_t block[BLOCK_SIZE];
    if (!read_directory_block(current_dir_inode, block)) {
        print_error("ls", "gagal membaca direktori", NULL);
        return false;
    }

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    while ((entry = next_entry(block, &offset)) != NULL) {
        if (is_dot_entry(entry))
            continue;
        out_buffer(entry_name(entry), entry->name_len);
        if (entry->file_type == EXT2_FT_DIR)
            out_char('/');
        out_char('\n');
    }
    return true;
}

static bool cmd_cd(const char *target) {
    uint32_t inode;
    uint8_t file_type;

    if (strcmp(target, "/") == 0) {
        current_dir_inode = ROOT_INODE;
        strcpy(cwd_name, "/");
        return true;
    }
    if (strcmp(target, ".") == 0)
        return true;

    if (!find_entry(current_dir_inode, target, &inode, &file_type) || file_type != EXT2_FT_DIR) {
        print_error("cd", "tidak ada direktori bernama", target);
        return false;
    }

    if (strcmp(target, "..") == 0) {
        uint32_t len = strlen(cwd_name);
        while (len > 1 && cwd_name[len - 1] != '/')
            len--;
        cwd_name[len > 1 ? len - 1 : 1] = '\0';
    } else {
        uint32_t len = strlen(cwd_name);
        uint32_t name_len = strlen(target);
        if (len + name_len + 2 >= MAX_PATH_LEN) {
            print_error("cd", "path terlalu panjang", NULL);
            return false;
        }
        if (len > 1)
            cwd_name[len++] = '/';
        strcpy(cwd_name + len, target);
    }
    current_dir_inode = inode;
    return true;
}

static bool cmd_mkdir(const char *name) {
    int8_t retcode = write_entry(current_dir_inode, name, NULL, 0, true);
    if (retcode == 1)
        print_error("mkdir", "nama sudah dipakai:", name);
    else if (retcode != 0)
        print_error("mkdir", "gagal membuat folder", name);
    return retcode == 0;
}

static bool cmd_cat(const char *name) {
    if (name == NULL) {
        if (!has_input) {
            print_usage("cat <file>");
            return false;
        }
        out_buffer(input_data, input_len);
        return true;
    }

    uint32_t size = 0;
    int8_t retcode = load_file(current_dir_inode, name, &size);
    if (retcode == 1) {
        print_error("cat", "bukan sebuah file:", name);
    } else if (retcode != 0) {
        print_error("cat", "file tidak ditemukan:", name);
    } else {
        out_buffer((const char *) FILE_BUFFER, size);
        if (size == 0 || FILE_BUFFER[size - 1] != '\n')
            out_char('\n');
    }
    return retcode == 0;
}

static bool cmd_cp(const char *src, const char *dest, bool recursive) {
    uint32_t inode, dest_parent;
    uint8_t file_type;
    const char *dest_name;

    if (!find_entry(current_dir_inode, src, &inode, &file_type)) {
        print_error("cp", "tidak ditemukan:", src);
        return false;
    }
    if (file_type == EXT2_FT_DIR && !recursive) {
        print_error("cp", "gunakan -r untuk folder:", src);
        return false;
    }
    if (!resolve_destination(src, dest, &dest_parent, &dest_name) ||
        !prepare_destination("cp", src, dest_parent, dest_name))
        return false;
    if (!copy_tree(current_dir_inode, src, dest_parent, dest_name)) {
        print_error("cp", "gagal menyalin", src);
        return false;
    }
    return true;
}

static bool cmd_rm(const char *name, bool recursive) {
    uint32_t inode;
    uint8_t file_type;

    if (!find_entry(current_dir_inode, name, &inode, &file_type)) {
        print_error("rm", "tidak ditemukan:", name);
        return false;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        print_error("rm", "tidak boleh menghapus", name);
        return false;
    }
    if (file_type == EXT2_FT_DIR && !recursive) {
        print_error("rm", "gunakan -r untuk folder:", name);
        return false;
    }
    if (!remove_tree(current_dir_inode, name)) {
        print_error("rm", "gagal menghapus", name);
        return false;
    }
    return true;
}

static bool cmd_mv(const char *src, const char *dest) {
    uint32_t dest_parent;
    const char *dest_name;

    if (!resolve_destination(src, dest, &dest_parent, &dest_name) ||
        !prepare_destination("mv", src, dest_parent, dest_name))
        return false;
    if (!copy_tree(current_dir_inode, src, dest_parent, dest_name)) {
        print_error("mv", "gagal memindahkan", src);
        return false;
    }
    if (!remove_tree(current_dir_inode, src)) {
        print_error("mv", "gagal menghapus sumber", src);
        return false;
    }
    return true;
}

static void find_walk(uint32_t inode, uint32_t path_len, const char *target) {
    uint8_t block[BLOCK_SIZE];
    if (!read_directory_block(inode, block))
        return;

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    while ((entry = next_entry(block, &offset)) != NULL) {
        if (is_dot_entry(entry) || path_len + entry->name_len + 2 >= MAX_PATH_LEN)
            continue;

        uint32_t new_len = path_len + entry->name_len;
        bool is_directory = entry->file_type == EXT2_FT_DIR;
        memcpy(find_path + path_len, entry_name(entry), entry->name_len);
        find_path[new_len] = '\0';

        if (entry_name_equal(entry, target)) {
            out_str(find_path);
            if (is_directory)
                out_char('/');
            out_char('\n');
            find_count++;
        }
        if (is_directory) {
            find_path[new_len] = '/';
            find_walk(entry->inode, new_len + 1, target);
        }
    }
}

static bool cmd_find(const char *name) {
    strcpy(find_path, "/");
    find_count = 0;
    find_walk(ROOT_INODE, 1, name);
    if (find_count == 0) {
        print_error("find", "tidak ditemukan:", name);
        return false;
    }
    return true;
}

static bool line_contains(const char *line, uint32_t line_len, const char *pattern, uint32_t pattern_len) {
    if (pattern_len > line_len)
        return false;
    for (uint32_t i = 0; i + pattern_len <= line_len; i++)
        if (memcmp(line + i, pattern, pattern_len) == 0)
            return true;
    return false;
}

static bool cmd_grep(const char *pattern, const char *name) {
    const char *data = input_data;
    uint32_t size = input_len;

    if (name != NULL) {
        int8_t retcode = load_file(current_dir_inode, name, &size);
        if (retcode != 0) {
            print_error("grep", "file tidak ditemukan:", name);
            return false;
        }
        data = (const char *) FILE_BUFFER;
    } else if (!has_input) {
        print_usage("grep <pattern> [file]");
        return false;
    }

    uint32_t pattern_len = strlen(pattern);
    uint32_t start = 0;
    while (start < size) {
        uint32_t end = start;
        while (end < size && data[end] != '\n')
            end++;
        if (line_contains(data + start, end - start, pattern, pattern_len)) {
            out_buffer(data + start, end - start);
            out_char('\n');
        }
        start = end + 1;
    }
    return true;
}

static bool cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        out_str(argv[i]);
        out_char(i + 1 < argc ? ' ' : '\n');
    }
    return true;
}

static bool cmd_clear(void) {
    syscall(8, 0, 0, 0);
    return true;
}

static uint32_t parse_uint(const char *str) {
    uint32_t value = 0;
    while (*str >= '0' && *str <= '9')
        value = value * 10 + (*str++ - '0');
    return value;
}

static bool cmd_memtest(const char *count_arg) {
    uint32_t pages = count_arg != NULL ? parse_uint(count_arg) : MEMTEST_MAX_PAGES;
    if (pages == 0 || pages > MEMTEST_MAX_PAGES) {
        print_usage("memtest [1-496]");
        return false;
    }

    for (uint32_t i = 0; i < pages; i++) {
        volatile uint32_t *page = (volatile uint32_t *) (MEMTEST_BASE + i * MEMTEST_PAGE_SIZE);
        page[0] = 0xCAFE0000u + i;
        page[1023] = 0xBEEF0000u + i;
    }
    for (uint32_t i = 0; i < pages; i++) {
        volatile uint32_t *page = (volatile uint32_t *) (MEMTEST_BASE + i * MEMTEST_PAGE_SIZE);
        if (page[0] != 0xCAFE0000u + i || page[1023] != 0xBEEF0000u + i) {
            print_error("memtest", "data rusak", NULL);
            return false;
        }
    }

    uint32_t swap_used = 0;
    syscall(10, (uint32_t) &swap_used, 0, 0);
    out_str("memtest OK: ");
    out_uint(pages);
    out_str(" halaman, swap terpakai ");
    out_uint(swap_used);
    out_str(" halaman\n");
    return true;
}

static bool cmd_help(void) {
    out_str("cd <dir>           pindah direktori (.. naik, / ke root)\n");
    out_str("ls                 isi direktori sekarang\n");
    out_str("mkdir <nama>       buat folder\n");
    out_str("cat <file>         tampilkan file\n");
    out_str("cp [-r] <a> <b>    salin file/folder\n");
    out_str("rm [-r] <nama>     hapus file/folder\n");
    out_str("mv <a> <b>         pindah atau rename\n");
    out_str("find <nama>        cari di seluruh file system\n");
    out_str("grep <pola> [file] cari teks, bisa dari pipeline\n");
    out_str("echo <teks>        tulis teks\n");
    out_str("clear              bersihkan layar\n");
    out_str("memtest [n]        tes memori dan swap\n");
    out_str("Pipeline: ls | grep a    Tulis file: echo halo > a.txt\n");
    return true;
}

static int tokenize_input(char *input, char *argv[]) {
    int argc = 0;
    char *ptr = input;

    while (*ptr != '\0') {
        while (*ptr == ' ')
            ptr++;
        if (*ptr == '\0')
            break;
        if (argc >= MAX_ARGS)
            break;

        argv[argc++] = ptr;
        while (*ptr != ' ' && *ptr != '\0')
            ptr++;
        if (*ptr != '\0')
            *ptr++ = '\0';
    }
    return argc;
}

static bool execute_command(int argc, char *argv[]) {
    const char *command = argv[0];
    bool recursive = argc > 1 && strcmp(argv[1], "-r") == 0;
    int first = recursive ? 2 : 1;

    if (strcmp(command, "cd") == 0) {
        if (argc > 1)
            return cmd_cd(argv[1]);
        print_usage("cd <directory>");
    } else if (strcmp(command, "ls") == 0) {
        return cmd_ls();
    } else if (strcmp(command, "mkdir") == 0) {
        if (argc > 1)
            return cmd_mkdir(argv[1]);
        print_usage("mkdir <foldername>");
    } else if (strcmp(command, "cat") == 0) {
        return cmd_cat(argc > 1 ? argv[1] : NULL);
    } else if (strcmp(command, "cp") == 0) {
        if (argc > first + 1)
            return cmd_cp(argv[first], argv[first + 1], recursive);
        print_usage("cp [-r] <src> <dest>");
    } else if (strcmp(command, "rm") == 0) {
        if (argc > first)
            return cmd_rm(argv[first], recursive);
        print_usage("rm [-r] <nama>");
    } else if (strcmp(command, "mv") == 0) {
        if (argc > 2)
            return cmd_mv(argv[1], argv[2]);
        print_usage("mv <src> <dest>");
    } else if (strcmp(command, "find") == 0) {
        if (argc > 1)
            return cmd_find(argv[1]);
        print_usage("find <nama>");
    } else if (strcmp(command, "grep") == 0) {
        if (argc > 1)
            return cmd_grep(argv[1], argc > 2 ? argv[2] : NULL);
        print_usage("grep <pattern> [file]");
    } else if (strcmp(command, "echo") == 0) {
        return cmd_echo(argc, argv);
    } else if (strcmp(command, "clear") == 0) {
        return cmd_clear();
    } else if (strcmp(command, "help") == 0) {
        return cmd_help();
    } else if (strcmp(command, "memtest") == 0) {
        return cmd_memtest(argc > 1 ? argv[1] : NULL);
    } else {
        print_error("shell", "perintah tidak dikenal:", command);
    }
    return false;
}

static int split_pipeline(char *line, char *stages[]) {
    int count = 0;
    stages[count++] = line;
    for (char *ptr = line; *ptr != '\0'; ptr++) {
        if (*ptr == '|') {
            *ptr = '\0';
            if (count >= MAX_PIPELINE_STAGES)
                return -1;
            stages[count++] = ptr + 1;
        }
    }
    return count;
}

static bool write_redirect(const char *name) {
    uint32_t inode;
    uint8_t file_type;
    if (find_entry(current_dir_inode, name, &inode, &file_type)) {
        if (file_type == EXT2_FT_DIR) {
            print_error("shell", "tujuan berupa folder:", name);
            return false;
        }
        delete_entry(current_dir_inode, name, false);
    }
    if (write_entry(current_dir_inode, name, output_buffer, output_len, false) != 0) {
        print_error("shell", "gagal menulis file", name);
        return false;
    }
    return true;
}

static void flush_output(void) {
    if (output_len == 0)
        return;
    sys_write_buffer(output_buffer, output_len, COLOR_OUTPUT);
    if (output_buffer[output_len - 1] != '\n')
        sys_putchar('\n', COLOR_OUTPUT);
}

static void run_pipeline(char *line) {
    char *stages[MAX_PIPELINE_STAGES];
    int stage_count = split_pipeline(line, stages);
    if (stage_count < 0) {
        print_error("shell", "pipeline terlalu panjang", NULL);
        return;
    }

    char *redirect_name = NULL;
    has_input = false;
    input_len = 0;
    output_len = 0;

    for (int i = 0; i < stage_count; i++) {
        char *argv[MAX_ARGS] = {0};
        int argc = tokenize_input(stages[i], argv);
        if (argc == 0) {
            if (stage_count > 1)
                print_error("shell", "pipeline tidak lengkap", NULL);
            return;
        }

        if (i == stage_count - 1 && argc >= 3 && strcmp(argv[argc - 2], ">") == 0) {
            redirect_name = argv[argc - 1];
            argc -= 2;
        }

        output_buffer = (i % 2 == 0) ? PIPE_BUFFER_A : PIPE_BUFFER_B;
        output_len = 0;
        if (!execute_command(argc, argv))
            return;

        input_data = output_buffer;
        input_len = output_len;
        has_input = true;
    }

    if (redirect_name != NULL)
        write_redirect(redirect_name);
    else
        flush_output();
}

static void read_line(char *input_buf) {
    int input_idx = 0;
    memset(input_buf, 0, MAX_INPUT_LEN);

    while (true) {
        char c = 0;
        sys_getchar(&c);

        if (c == '\n') {
            sys_putchar('\n', COLOR_OUTPUT);
            return;
        } else if (c == '\b') {
            if (input_idx > 0) {
                input_buf[--input_idx] = '\0';
                sys_putchar('\b', COLOR_OUTPUT);
            }
        } else if (c >= 32 && c <= 126) {
            if (input_idx < MAX_INPUT_LEN - 1) {
                input_buf[input_idx++] = c;
                sys_putchar(c, COLOR_OUTPUT);
            }
        }
    }
}

int main(void) {
    char input_buf[MAX_INPUT_LEN];

    syscall(7, 0, 0, 0);
    sys_puts("Selamat datang di IF2130 Shell! Ketik help untuk daftar perintah.\n", COLOR_WELCOME);

    while (true) {
        print_prompt();
        read_line(input_buf);
        run_pipeline(input_buf);
    }

    return 0;
}
