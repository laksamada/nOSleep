#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h"
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

#define ROOT_INODE 1
#define MANY_FILES 10

uint32_t find_inode_by_name(uint32_t parent_inode_num, char *target_name, uint8_t name_len);

static uint8_t source[24 * BLOCK_SIZE];
static uint8_t target[24 * BLOCK_SIZE];
static uint8_t test_count = 0;
static uint8_t failed = 0;

static uint8_t text_length(const char *text)
{
    uint8_t length = 0;
    while (text[length] != '\0')
        length++;
    return length;
}

static void print_text(uint8_t row, uint8_t col, const char *text, uint8_t color)
{
    for (uint8_t i = 0; text[i] != '\0'; i++)
        framebuffer_write(row, col + i, text[i], color, 0);
}

static void check(const char *label, int8_t result, int8_t expected)
{
    uint8_t row = 2 + test_count % 21;
    uint8_t col = test_count / 21 * 40;
    char got[3] = {result < 0 ? '-' : ' ', '0' + (result < 0 ? -result : result), '\0'};
    char want[3] = {expected < 0 ? '-' : ' ', '0' + (expected < 0 ? -expected : expected), '\0'};
    print_text(row, col, label, 0xF);
    print_text(row, col + 25, got, 0xF);
    print_text(row, col + 27, "/", 0x7);
    print_text(row, col + 28, want, 0x7);
    print_text(row, col + 31, result == expected ? "OK" : "FAIL", result == expected ? 0xA : 0xC);
    if (result != expected)
        failed++;
    test_count++;
}

static void fill(uint32_t size, uint8_t seed)
{
    for (uint32_t i = 0; i < size; i++)
        source[i] = (uint8_t)((i * 31 + seed * 7 + 7) % 251);
}

static struct EXT2DriverRequest make_request(uint32_t parent_inode, char *name, void *buf, uint32_t buffer_size, bool is_directory)
{
    struct EXT2DriverRequest request = {
        .buf          = buf,
        .name         = name,
        .name_len     = text_length(name),
        .parent_inode = parent_inode,
        .buffer_size  = buffer_size,
        .is_directory = is_directory,
    };
    return request;
}

static int8_t make_folder(uint32_t parent_inode, char *name)
{
    struct EXT2DriverRequest request = make_request(parent_inode, name, 0, 0, true);
    return write(&request);
}

static int8_t write_file(uint32_t parent_inode, char *name, uint32_t size, uint8_t seed)
{
    fill(size, seed);
    struct EXT2DriverRequest request = make_request(parent_inode, name, source, size, false);
    return write(&request);
}

static int8_t read_file(uint32_t parent_inode, char *name, uint32_t buffer_size)
{
    return read(make_request(parent_inode, name, target, buffer_size, false));
}

static int8_t verify_file(uint32_t parent_inode, char *name, uint32_t size, uint8_t seed)
{
    memset(target, 0, sizeof(target));
    int8_t code = read_file(parent_inode, name, sizeof(target));
    if (code != 0)
        return code;
    fill(size, seed);
    return memcmp(source, target, size) == 0 ? 0 : -1;
}

static int8_t list_folder(uint32_t parent_inode, char *name, char *child)
{
    memset(target, 0, BLOCK_SIZE);
    struct EXT2DriverRequest request = make_request(parent_inode, name, target, BLOCK_SIZE, true);
    int8_t code = read_directory(&request);
    if (code != 0 || child == 0)
        return code;

    uint8_t length = text_length(child);
    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(target + offset);
        if (entry->inode == 0 || entry->rec_len == 0)
            break;
        if (entry->name_len == length && memcmp(get_entry_name(entry), child, length) == 0)
            return 0;
        offset += entry->rec_len;
    }
    return -1;
}

static int8_t remove_entry(uint32_t parent_inode, char *name, bool is_directory)
{
    return delete(make_request(parent_inode, name, 0, 0, is_directory));
}

static int8_t write_many(void)
{
    char name[3] = {'m', '0', '\0'};
    for (uint8_t i = 0; i < MANY_FILES; i++) {
        name[1] = '0' + i;
        int8_t code = write_file(ROOT_INODE, name, 300 + i * 50, 20 + i);
        if (code != 0)
            return code;
    }
    return 0;
}

static int8_t verify_many(void)
{
    char name[3] = {'m', '0', '\0'};
    for (uint8_t i = 0; i < MANY_FILES; i++) {
        name[1] = '0' + i;
        int8_t code = verify_file(ROOT_INODE, name, 300 + i * 50, 20 + i);
        if (code != 0)
            return code;
    }
    return 0;
}

static void first_boot(void)
{
    print_text(0, 0, "BOOT 1: tes CRUD di disk kosong", 0xE);

    check("W01 mkdir folder1", make_folder(ROOT_INODE, "folder1"), 0);
    check("W02 mkdir folder1 lagi", make_folder(ROOT_INODE, "folder1"), 1);
    check("W03 write kecil 100B", write_file(ROOT_INODE, "kecil", 100, 1), 0);
    check("W04 write pas512 512B", write_file(ROOT_INODE, "pas512", BLOCK_SIZE, 2), 0);
    check("W05 write besar 20 blok", write_file(ROOT_INODE, "besar", 20 * BLOCK_SIZE - 3, 3), 0);
    uint32_t folder1 = find_inode_by_name(ROOT_INODE, "folder1", 7);
    uint32_t kecil = find_inode_by_name(ROOT_INODE, "kecil", 5);
    check("W06 write folder1/xi", write_file(folder1, "xi", 700, 4), 0);
    check("W07 write parent=file", write_file(kecil, "x", 10, 5), 2);
    check("W08 write parent=0", write_file(0, "x", 10, 5), 2);

    check("R01 read kecil utuh", verify_file(ROOT_INODE, "kecil", 100, 1), 0);
    check("R02 read pas512 utuh", verify_file(ROOT_INODE, "pas512", BLOCK_SIZE, 2), 0);
    check("R03 read besar utuh", verify_file(ROOT_INODE, "besar", 20 * BLOCK_SIZE - 3, 3), 0);
    check("R04 read folder1", read_file(ROOT_INODE, "folder1", sizeof(target)), 1);
    check("R05 read buffer kurang", read_file(ROOT_INODE, "besar", BLOCK_SIZE), 2);
    check("R06 read tidakada", read_file(ROOT_INODE, "tidakada", sizeof(target)), 3);
    check("R07 read parent=file", read_file(kecil, "x", sizeof(target)), 4);

    check("L01 readdir folder1", list_folder(ROOT_INODE, "folder1", "xi"), 0);
    check("L02 readdir kecil", list_folder(ROOT_INODE, "kecil", 0), 1);
    check("L03 readdir tidakada", list_folder(ROOT_INODE, "tidakada", 0), 2);
    check("L04 readdir parent=file", list_folder(kecil, "x", 0), 3);

    check("D01 del folder1 berisi", remove_entry(ROOT_INODE, "folder1", true), 2);
    check("D02 del folder1/xi", remove_entry(folder1, "xi", false), 0);
    check("D03 del folder1", remove_entry(ROOT_INODE, "folder1", true), 0);
    check("D04 del kecil", remove_entry(ROOT_INODE, "kecil", false), 0);
    check("D05 read kecil terhapus", read_file(ROOT_INODE, "kecil", sizeof(target)), 3);

    check("W09 write ulang kecil", write_file(ROOT_INODE, "kecil", 900, 9), 0);
    check("R08 read kecil baru", verify_file(ROOT_INODE, "kecil", 900, 9), 0);
    check("W10 write 10 file m0-m9", write_many(), 0);
    check("R09 read 10 file m0-m9", verify_many(), 0);
    check("W11 write penanda boot", write_file(ROOT_INODE, "persist", 4, 0), 0);
}

static void second_boot(void)
{
    print_text(0, 0, "BOOT 2: data harus masih ada setelah restart", 0xE);

    check("P01 read besar utuh", verify_file(ROOT_INODE, "besar", 20 * BLOCK_SIZE - 3, 3), 0);
    check("P02 read pas512 utuh", verify_file(ROOT_INODE, "pas512", BLOCK_SIZE, 2), 0);
    check("P03 read kecil baru", verify_file(ROOT_INODE, "kecil", 900, 9), 0);
    check("P04 read 10 file m0-m9", verify_many(), 0);
    check("P05 readdir root", list_folder(ROOT_INODE, "", "persist"), 0);
    check("P06 folder1 tetap hilang", list_folder(ROOT_INODE, "folder1", 0), 2);
    check("P07 mkdir folder2", make_folder(ROOT_INODE, "folder2"), 0);
    uint32_t folder2 = find_inode_by_name(ROOT_INODE, "folder2", 7);
    check("P08 write folder2/isi", write_file(folder2, "isi", 1500, 11), 0);
    check("P09 read folder2/isi", verify_file(folder2, "isi", 1500, 11), 0);
}

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    activate_keyboard_interrupt();
    initialize_idt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    initialize_filesystem_ext2();

    if (read_file(ROOT_INODE, "persist", sizeof(target)) == 0)
        second_boot();
    else
        first_boot();

    print_text(24, 0, failed == 0 ? "SEMUA TES LULUS" : "ADA TES GAGAL", failed == 0 ? 0xA : 0xC);
    while (true);
}
