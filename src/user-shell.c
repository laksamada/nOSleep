#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"
#include "header/process/syscall.h"

#define MAX_INPUT_LEN 256
#define MAX_ARGS 16
#define MAX_PIPELINE_STAGES 8
#define MAX_PATH_LEN 256
#define MAX_NAME_LEN 256
#define ROOT_INODE 1

#define IO_BUFFER_SIZE 0x40000u
#define DIR_BUFFER_SIZE (4u * BLOCK_SIZE)
#define MAX_FILE_SIZE  ((12u + BLOCK_SIZE / 4u) * BLOCK_SIZE)
#define FILE_BUFFER    ((uint8_t *) 0x100000)
#define PIPE_BUFFER_A  ((char *) 0x140000)
#define PIPE_BUFFER_B  ((char *) 0x180000)
#define BACKUP_BUFFER  ((uint8_t *) 0x1C0000)

#define MEMTEST_BASE      0x200000u
#define MEMTEST_MAX_PAGES 496u
#define MEMTEST_PAGE_SIZE 0x1000u

#define COLOR_OUTPUT 0x0F
#define COLOR_ERROR  0x0C
#define COLOR_USAGE  0x0E
#define COLOR_PROMPT 0x0B
#define COLOR_WELCOME 0x0A

#define PROGRAM_SIZE_MAX    0x200000u
#define PROCESS_LIST_MAX    16
#define FOREGROUND_GRACE_MS 100u

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
    __asm__ volatile("int $0x30"
        : "+a"(eax), "+b"(ebx), "+c"(ecx), "+d"(edx)
        : /* <Empty> */
        : "memory");
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

static void sys_yield(void) {
    syscall(SYSCALL_YIELD, 0, 0, 0);
}

static uint32_t sys_get_ticks(void) {
    uint32_t ticks = 0;
    syscall(SYSCALL_GET_TICKS, (uint32_t) &ticks, 0, 0);
    return ticks;
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
    sys_puts("Usage: ", COLOR_USAGE);
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

static bool read_directory_data(uint32_t inode, uint8_t *buf, uint32_t *out_size) {
    uint32_t dir_size = 0;
    syscall(9, inode, (uint32_t) &dir_size, 0);
    if (dir_size == 0)
        dir_size = BLOCK_SIZE;
    if (dir_size > DIR_BUFFER_SIZE)
        dir_size = DIR_BUFFER_SIZE;
    struct EXT2DriverRequest request = {
        .buf          = buf,
        .name         = NULL,
        .name_len     = 0,
        .parent_inode = inode,
        .buffer_size  = dir_size,
    };
    int8_t retcode = -1;
    syscall(1, (uint32_t) &request, (uint32_t) &retcode, 0);
    if (retcode != 0)
        return false;
    *out_size = dir_size;
    return true;
}

static struct EXT2DirectoryEntry *next_entry(uint8_t *block, uint32_t *offset) {
    if (*offset + sizeof(struct EXT2DirectoryEntry) > BLOCK_SIZE)
        return NULL;
    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (block + *offset);
    if (entry->inode == 0 || entry->rec_len < sizeof(struct EXT2DirectoryEntry) ||
        entry->rec_len > BLOCK_SIZE - *offset)
        return NULL;
    if (entry->name_len > entry->rec_len - sizeof(struct EXT2DirectoryEntry))
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
    uint8_t buf[DIR_BUFFER_SIZE];
    uint32_t dir_size = 0;
    if (!read_directory_data(parent, buf, &dir_size))
        return false;

    for (uint32_t blk = 0; blk + BLOCK_SIZE <= dir_size; blk += BLOCK_SIZE) {
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *entry;
        while ((entry = next_entry(buf + blk, &offset)) != NULL) {
            if (entry_name_equal(entry, name)) {
                *inode = entry->inode;
                *file_type = entry->file_type;
                return true;
            }
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

static int8_t load_file_to(uint32_t parent, const char *name, uint8_t *dest, uint32_t *size) {
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
        .buf          = dest,
        .name         = (char *) name,
        .name_len     = strlen(name),
        .parent_inode = parent,
        .buffer_size  = IO_BUFFER_SIZE,
    };
    int8_t retcode = -1;
    syscall(0, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode;
}

static int8_t load_file(uint32_t parent, const char *name, uint32_t *size) {
    return load_file_to(parent, name, FILE_BUFFER, size);
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

    uint8_t buf[DIR_BUFFER_SIZE];
    uint32_t dir_size = 0;
    if (!read_directory_data(inode, buf, &dir_size))
        return false;

    for (uint32_t blk = 0; blk + BLOCK_SIZE <= dir_size; blk += BLOCK_SIZE) {
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *entry;
        while ((entry = next_entry(buf + blk, &offset)) != NULL) {
            if (is_dot_entry(entry))
                continue;
            char child[MAX_NAME_LEN];
            copy_entry_name(entry, child);
            if (!copy_tree(inode, child, dest_inode, child))
                return false;
        }
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

    uint8_t buf[DIR_BUFFER_SIZE];
    uint32_t dir_size = 0;
    if (!read_directory_data(inode, buf, &dir_size))
        return false;

    for (uint32_t blk = 0; blk + BLOCK_SIZE <= dir_size; blk += BLOCK_SIZE) {
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *entry;
        while ((entry = next_entry(buf + blk, &offset)) != NULL) {
            if (is_dot_entry(entry))
                continue;
            char child[MAX_NAME_LEN];
            copy_entry_name(entry, child);
            if (!remove_tree(inode, child))
                return false;
        }
    }
    return delete_entry(parent, name, true) == 0;
}

static bool is_ancestor_dir(uint32_t ancestor, uint32_t dir) {
    for (int step = 0; step < 64; step++) {
        if (dir == ancestor)
            return true;
        if (dir == ROOT_INODE)
            return false;
        uint32_t parent_inode;
        uint8_t file_type;
        if (!find_entry(dir, "..", &parent_inode, &file_type) || parent_inode == dir)
            return false;
        dir = parent_inode;
    }
    return false;
}

static bool write_file_safe(uint32_t parent, const char *name, const void *buf, uint32_t size, bool overwrite) {
    if (size > MAX_FILE_SIZE)
        return false;
    if (!overwrite)
        return write_entry(parent, name, (void *) buf, size, false) == 0;

    uint32_t old_size = 0;
    bool backed_up = load_file_to(parent, name, BACKUP_BUFFER, &old_size) == 0;
    if (!backed_up)
        return false;

    if (delete_entry(parent, name, false) != 0)
        return false;

    if (write_entry(parent, name, (void *) buf, size, false) == 0)
        return true;

    write_entry(parent, name, BACKUP_BUFFER, old_size, false);
    return false;
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

static bool prepare_destination(const char *command, const char *src, uint32_t dest_parent,
                                const char *dest_name, bool *overwrite) {
    uint32_t src_inode, dest_inode;
    uint8_t src_type, dest_type;

    *overwrite = false;
    if (strcmp(dest_name, ".") == 0 || strcmp(dest_name, "..") == 0) {
        print_error(command, "invalid destination:", dest_name);
        return false;
    }
    if (!find_entry(current_dir_inode, src, &src_inode, &src_type)) {
        print_error(command, "not found:", src);
        return false;
    }
    if (src_inode == dest_parent || (src_type == EXT2_FT_DIR && is_ancestor_dir(src_inode, dest_parent))) {
        print_error(command, "cannot copy or move into itself:", src);
        return false;
    }
    if (!find_entry(dest_parent, dest_name, &dest_inode, &dest_type))
        return true;

    if (dest_inode == src_inode) {
        print_error(command, "source and destination are the same:", src);
        return false;
    }
    if (dest_type == EXT2_FT_DIR) {
        print_error(command, "destination is an existing folder:", dest_name);
        return false;
    }
    if (src_type == EXT2_FT_DIR) {
        print_error(command, "cannot overwrite non-folder with folder:", dest_name);
        return false;
    }
    *overwrite = true;
    return true;
}

static bool cmd_ls(void) {
    uint8_t buf[DIR_BUFFER_SIZE];
    uint32_t dir_size = 0;
    if (!read_directory_data(current_dir_inode, buf, &dir_size)) {
        print_error("ls", "failed to read directory", NULL);
        return false;
    }

    for (uint32_t blk = 0; blk + BLOCK_SIZE <= dir_size; blk += BLOCK_SIZE) {
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *entry;
        while ((entry = next_entry(buf + blk, &offset)) != NULL) {
            if (is_dot_entry(entry))
                continue;
            out_buffer(entry_name(entry), entry->name_len);
            if (entry->file_type == EXT2_FT_DIR)
                out_char('/');
            out_char('\n');
        }
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
        print_error("cd", "no such directory:", target);
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
            print_error("cd", "path too long", NULL);
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
        print_error("mkdir", "name already exists:", name);
    else if (retcode != 0)
        print_error("mkdir", "failed to create folder", name);
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
        print_error("cat", "not a file:", name);
    } else if (retcode != 0) {
        print_error("cat", "file not found:", name);
    } else {
        out_buffer((const char *) FILE_BUFFER, size);
    }
    return retcode == 0;
}

static bool cmd_cp(const char *src, const char *dest, bool recursive) {
    uint32_t inode, dest_parent;
    uint8_t file_type;
    const char *dest_name;
    bool overwrite = false;

    if (!find_entry(current_dir_inode, src, &inode, &file_type)) {
        print_error("cp", "not found:", src);
        return false;
    }
    if (file_type == EXT2_FT_DIR && !recursive) {
        print_error("cp", "use -r for folders:", src);
        return false;
    }
    if (!resolve_destination(src, dest, &dest_parent, &dest_name) ||
        !prepare_destination("cp", src, dest_parent, dest_name, &overwrite))
        return false;

    if (file_type != EXT2_FT_DIR) {
        uint32_t size = 0;
        if (load_file(current_dir_inode, src, &size) != 0 ||
            !write_file_safe(dest_parent, dest_name, FILE_BUFFER, size, overwrite)) {
            print_error("cp", "failed to copy", src);
            return false;
        }
        return true;
    }
    if (!copy_tree(current_dir_inode, src, dest_parent, dest_name)) {
        print_error("cp", "failed to copy", src);
        return false;
    }
    return true;
}

static bool cmd_rm(const char *name, bool recursive) {
    uint32_t inode;
    uint8_t file_type;

    if (!find_entry(current_dir_inode, name, &inode, &file_type)) {
        print_error("rm", "not found:", name);
        return false;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        print_error("rm", "cannot remove", name);
        return false;
    }
    if (file_type == EXT2_FT_DIR && !recursive) {
        print_error("rm", "use -r for folders:", name);
        return false;
    }
    if (!remove_tree(current_dir_inode, name)) {
        print_error("rm", "failed to remove", name);
        return false;
    }
    return true;
}

static bool cmd_mv(const char *src, const char *dest) {
    uint32_t inode, dest_parent;
    uint8_t file_type;
    const char *dest_name;
    bool overwrite = false;

    if (strcmp(src, ".") == 0 || strcmp(src, "..") == 0) {
        print_error("mv", "cannot move", src);
        return false;
    }
    if (!find_entry(current_dir_inode, src, &inode, &file_type)) {
        print_error("mv", "not found:", src);
        return false;
    }
    if (!resolve_destination(src, dest, &dest_parent, &dest_name) ||
        !prepare_destination("mv", src, dest_parent, dest_name, &overwrite))
        return false;

    bool copied = false;
    if (file_type != EXT2_FT_DIR) {
        uint32_t size = 0;
        copied = load_file(current_dir_inode, src, &size) == 0 &&
                 write_file_safe(dest_parent, dest_name, FILE_BUFFER, size, overwrite);
    } else {
        copied = copy_tree(current_dir_inode, src, dest_parent, dest_name);
    }
    if (!copied) {
        print_error("mv", "failed to move", src);
        return false;
    }
    if (!remove_tree(current_dir_inode, src)) {
        print_error("mv", "failed to remove source", src);
        return false;
    }
    return true;
}

static void find_walk(uint32_t inode, uint32_t path_len, const char *target) {
    uint8_t buf[DIR_BUFFER_SIZE];
    uint32_t dir_size = 0;
    if (!read_directory_data(inode, buf, &dir_size))
        return;

    for (uint32_t blk = 0; blk + BLOCK_SIZE <= dir_size; blk += BLOCK_SIZE) {
        uint32_t offset = 0;
        struct EXT2DirectoryEntry *entry;
        while ((entry = next_entry(buf + blk, &offset)) != NULL) {
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
}

static bool cmd_find(const char *name) {
    strcpy(find_path, "/");
    find_count = 0;
    find_walk(ROOT_INODE, 1, name);
    if (find_count == 0) {
        print_error("find", "not found:", name);
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
            print_error("grep", "file not found:", name);
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
            print_error("memtest", "data corrupted", NULL);
            return false;
        }
    }

    uint32_t swap_used = 0;
    syscall(10, (uint32_t) &swap_used, 0, 0);
    out_str("memtest OK: ");
    out_uint(pages);
    out_str(" pages, swap used ");
    out_uint(swap_used);
    out_str(" pages\n");
    return true;
}

static void out_column(const char *text, uint32_t width) {
    uint32_t len = strlen(text);
    out_str(text);
    while (len++ < width)
        out_char(' ');
}

static void uint_to_string(uint32_t value, char *text) {
    char digits[10];
    int count = 0;
    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value != 0);
    while (count > 0)
        *text++ = digits[--count];
    *text = '\0';
}

static const char *exec_error_message(int32_t retcode) {
    switch (retcode) {
        case 1:  return "process limit reached:";
        case 2:  return "invalid entrypoint:";
        case 3:  return "not enough memory for";
        default: return "program not found:";
    }
}

static void query_process(uint32_t pid, bool *alive, bool *interactive) {
    *alive = false;
    *interactive = false;
    syscall(SYSCALL_PROCESS_ALIVE, pid, (uint32_t) alive, (uint32_t) interactive);
}

static bool parse_uint_strict(const char *str, uint32_t *out) {
    if (str == NULL || *str == '\0')
        return false;
    uint32_t value = 0;
    while (*str != '\0') {
        if (*str < '0' || *str > '9')
            return false;
        value = value * 10 + (*str++ - '0');
    }
    *out = value;
    return true;
}

static bool resolve_program_path(const char *path, uint32_t *parent_inode, char *basename, bool *has_slash) {
    if (path == NULL || path[0] == '\0')
        return false;

    uint32_t len = strlen(path);
    bool contains_slash = false;
    for (uint32_t i = 0; i < len; i++) {
        if (path[i] == '/') {
            contains_slash = true;
            break;
        }
    }

    if (!contains_slash) {
        if (strcmp(path, ".") == 0 || strcmp(path, "..") == 0 || len >= MAX_NAME_LEN)
            return false;
        *has_slash = false;
        *parent_inode = current_dir_inode;
        strcpy(basename, path);
        return true;
    }

    *has_slash = true;
    if (path[len - 1] == '/')
        return false;

    uint32_t dir = (path[0] == '/') ? ROOT_INODE : current_dir_inode;
    const char *p = path;
    while (*p != '\0') {
        while (*p == '/')
            p++;
        if (*p == '\0')
            return false;

        char seg[MAX_NAME_LEN];
        uint32_t seg_len = 0;
        while (p[seg_len] != '\0' && p[seg_len] != '/') {
            if (seg_len + 1 >= MAX_NAME_LEN)
                return false;
            seg[seg_len] = p[seg_len];
            seg_len++;
        }
        seg[seg_len] = '\0';
        p += seg_len;

        if (*p == '\0') {
            if (strcmp(seg, ".") == 0 || strcmp(seg, "..") == 0)
                return false;
            strcpy(basename, seg);
            *parent_inode = dir;
            return true;
        }

        if (strcmp(seg, ".") == 0)
            continue;

        uint32_t next_inode;
        uint8_t file_type;
        if (!find_entry(dir, seg, &next_inode, &file_type) || file_type != EXT2_FT_DIR)
            return false;
        dir = next_inode;
    }
    return false;
}

static bool hosts_terminal(const char *name) {
    return strcmp(name, "gui") == 0;
}

static bool run_program(const char *name, bool background, bool as_command) {
    uint32_t parent_inode = current_dir_inode;
    char prog_name[MAX_NAME_LEN];
    bool has_slash = false;

    if (!resolve_program_path(name, &parent_inode, prog_name, &has_slash)) {
        if (as_command)
            print_error("shell", "command not found:", name);
        else
            print_error("exec", "program not found:", name);
        return false;
    }
    background = background || hosts_terminal(prog_name);

    struct EXT2DriverRequest request = {
        .buf          = (uint8_t *) 0,
        .name         = prog_name,
        .name_len     = strlen(prog_name),
        .parent_inode = parent_inode,
        .buffer_size  = PROGRAM_SIZE_MAX,
    };
    int32_t retcode = -1;
    uint32_t pid = 0;
    syscall(SYSCALL_EXEC, (uint32_t) &request, (uint32_t) &retcode, (uint32_t) &pid);
    if (retcode == 4 && !has_slash && parent_inode != ROOT_INODE) {
        request.parent_inode = ROOT_INODE;
        syscall(SYSCALL_EXEC, (uint32_t) &request, (uint32_t) &retcode, (uint32_t) &pid);
    }

    if (retcode != 0) {
        if (as_command && retcode == 4)
            print_error("shell", "command not found:", name);
        else
            print_error("exec", exec_error_message(retcode), name);
        return false;
    }

    if (!background) {
        uint32_t start = sys_get_ticks();
        bool alive = false;
        bool interactive = false;
        while (true) {
            query_process(pid, &alive, &interactive);
            if (!alive) {
                char leftover;
                sys_getchar(&leftover);
                return true;
            }
            if (!interactive && sys_get_ticks() - start >= FOREGROUND_GRACE_MS)
                break;
            sys_yield();
        }
    }

    char pid_text[12];
    uint_to_string(pid, pid_text);
    out_str("[");
    out_str(pid_text);
    out_str("] ");
    out_str(prog_name);
    out_str(" running in background\n");
    return true;
}

static bool cmd_ps(void) {
    struct ProcessInfo list[PROCESS_LIST_MAX];
    uint32_t count = 0;
    syscall(SYSCALL_PS, (uint32_t) list, PROCESS_LIST_MAX, (uint32_t) &count);

    out_str("PID   STATE     MEMORY    NAME\n");
    for (uint32_t i = 0; i < count; i++) {
        char number[12];
        uint_to_string(list[i].pid, number);
        out_column(number, 6);
        out_column(list[i].state == 2 ? "running" : "ready", 10);
        uint_to_string(list[i].frame_count * 4, number);
        strcpy(number + strlen(number), " KB");
        out_column(number, 10);
        out_str(list[i].name);
        out_char('\n');
    }
    return true;
}

static bool cmd_kill(const char *pid_text) {
    uint32_t pid = 0;
    if (!parse_uint_strict(pid_text, &pid)) {
        print_error("kill", "invalid PID:", pid_text);
        return false;
    }
    int32_t result = -1;
    syscall(SYSCALL_KILL, pid, (uint32_t) &result, 0);

    if (result == 1) {
        print_error("kill", "no process with PID", pid_text);
        return false;
    }
    if (result == 2) {
        print_error("kill", "shell (PID 1) cannot be killed", NULL);
        return false;
    }
    out_str("process ");
    out_str(pid_text);
    out_str(" killed\n");
    return true;
}

static bool cmd_exit(void) {
    struct ProcessInfo list[PROCESS_LIST_MAX];
    uint32_t count = 0;
    syscall(SYSCALL_PS, (uint32_t) list, PROCESS_LIST_MAX, (uint32_t) &count);

    uint32_t current_pid = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (list[i].state == 2) {
            current_pid = list[i].pid;
            break;
        }
    }
    if (current_pid <= 1) {
        print_error("exit", "shell (PID 1) cannot exit", NULL);
        return false;
    }

    syscall(SYSCALL_EXIT, 0, 0, 0);
    return true;
}

static bool cmd_help(void) {
    out_str("cd <dir>           change directory (.. up, / root)\n");
    out_str("ls                 list current directory\n");
    out_str("mkdir <name>       create folder\n");
    out_str("cat <file>         print file\n");
    out_str("cp [-r] <a> <b>    copy file or folder\n");
    out_str("rm [-r] <name>     remove file or folder\n");
    out_str("mv <a> <b>         move or rename\n");
    out_str("find <name>        search the whole file system\n");
    out_str("grep <pat> [file]  search text, works in pipelines\n");
    out_str("echo <text>        print text\n");
    out_str("clear              clear screen\n");
    out_str("memtest [n]        test memory and swap\n");
    out_str("exec <prog> [&]    run program (supports paths), & for background\n");
    out_str("<prog> [&]         same as exec, e.g. hello, clock, gui, badapple\n");
    out_str("ps                 list processes\n");
    out_str("kill <pid>         stop a process\n");
    out_str("exit               exit sub-shell (PID 1 protected)\n");
    out_str("Pipeline: ls | grep a    Write file: echo hello > a.txt\n");
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
    bool background = argc > 1 && strcmp(argv[argc - 1], "&") == 0;

    if (strcmp(command, "exec") == 0) {
        if (argc > 1 && strcmp(argv[1], "&") != 0)
            return run_program(argv[1], background, false);
        print_usage("exec <program> [&]");
    } else if (strcmp(command, "ps") == 0) {
        return cmd_ps();
    } else if (strcmp(command, "kill") == 0) {
        if (argc > 1)
            return cmd_kill(argv[1]);
        print_usage("kill <pid>");
    } else if (strcmp(command, "exit") == 0) {
        return cmd_exit();
    } else if (strcmp(command, "cd") == 0) {
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
        print_usage("rm [-r] <name>");
    } else if (strcmp(command, "mv") == 0) {
        if (argc > 2)
            return cmd_mv(argv[1], argv[2]);
        print_usage("mv <src> <dest>");
    } else if (strcmp(command, "find") == 0) {
        if (argc > 1)
            return cmd_find(argv[1]);
        print_usage("find <name>");
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
        return run_program(command, background, true);
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
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        print_error("shell", "invalid filename:", name);
        return false;
    }
    uint32_t inode;
    uint8_t file_type;
    bool exists = find_entry(current_dir_inode, name, &inode, &file_type);
    if (exists && file_type == EXT2_FT_DIR) {
        print_error("shell", "destination is a folder:", name);
        return false;
    }
    if (!write_file_safe(current_dir_inode, name, output_buffer, output_len, exists)) {
        print_error("shell", "failed to write file", name);
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
        print_error("shell", "pipeline too long", NULL);
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
                print_error("shell", "incomplete pipeline", NULL);
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
        if (c == 0) {
            sys_yield();
            continue;
        }

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
    sys_puts("Welcome to IF2130 Shell! Type help for a list of commands.\n", COLOR_WELCOME);

    while (true) {
        print_prompt();
        read_line(input_buf);
        run_pipeline(input_buf);
    }

    return 0;
}
