#include "header/filesystem/bootfile.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"

#define MULTIBOOT_FLAG_MODULES (1 << 3)

struct MultibootInfo {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
} __attribute__((packed));

struct MultibootModule {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed));

static struct BootFile bootfiles[BOOTFILE_COUNT_MAX];
static uint32_t bootfile_count;

static void *low_memory(uint32_t physical_addr) {
    return (void *) (physical_addr + KERNEL_VIRTUAL_ADDRESS_BASE);
}

static void copy_basename(char *dest, const char *path) {
    const char *name = path;
    for (const char *ptr = path; *ptr != '\0' && *ptr != ' '; ptr++)
        if (*ptr == '/')
            name = ptr + 1;

    uint32_t len = 0;
    while (name[len] != '\0' && name[len] != ' ' && len < BOOTFILE_NAME_LENGTH_MAX - 1) {
        dest[len] = name[len];
        len++;
    }
    dest[len] = '\0';
}

void bootfile_init(uint32_t multiboot_info_physical_addr) {
    struct MultibootInfo *info = low_memory(multiboot_info_physical_addr);
    if (multiboot_info_physical_addr == 0 || !(info->flags & MULTIBOOT_FLAG_MODULES) || info->mods_count == 0)
        return;

    struct MultibootModule *modules = low_memory(info->mods_addr);
    uint32_t first = modules[0].mod_start, last = modules[0].mod_end;
    for (uint32_t i = 1; i < info->mods_count; i++) {
        if (modules[i].mod_start < first)
            first = modules[i].mod_start;
        if (modules[i].mod_end > last)
            last = modules[i].mod_end;
    }

    uint32_t window_physical = first & ~(PAGE_4MB_SIZE - 1);
    paging_map_kernel_window(BOOTFILE_WINDOW_BASE, window_physical, last - window_physical);
    paging_reserve_physical_range(first, last - first);

    for (uint32_t i = 0; i < info->mods_count && bootfile_count < BOOTFILE_COUNT_MAX; i++) {
        struct BootFile *file = &bootfiles[bootfile_count++];
        copy_basename(file->name, low_memory(modules[i].string));
        file->data = (const uint8_t *) (BOOTFILE_WINDOW_BASE + modules[i].mod_start - window_physical);
        file->size = modules[i].mod_end - modules[i].mod_start;
    }
}

const struct BootFile* bootfile_find(const char *name, uint32_t name_len) {
    if (name_len >= BOOTFILE_NAME_LENGTH_MAX)
        return NULL;
    for (uint32_t i = 0; i < bootfile_count; i++) {
        const char *candidate = bootfiles[i].name;
        if (memcmp(candidate, name, name_len) == 0 && candidate[name_len] == '\0')
            return &bootfiles[i];
    }
    return NULL;
}
