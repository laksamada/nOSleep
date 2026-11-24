#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/stdlib/string.h"

/**
 * _paging_kernel_page_directory and _paging_kernel_page_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;

__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;
__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_tables[KERNEL_DIRECT_MAP_TABLE_COUNT - 1];

/**
 * Global page frame manager state tracking free/busy frames.
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
        [KERNEL_RESERVED_PAGE_FRAME_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = false
    },
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

struct PageDirectory* paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_page_directory_phys_addr) : /* <Empty> */);
    return (struct PageDirectory*) ((current_page_directory_phys_addr & 0xFFFFF000u) + KERNEL_VIRTUAL_ADDRESS_BASE);
}

void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr) : "memory");
}

static int32_t allocate_page_frame(void) {
    for (uint32_t frame = KERNEL_RESERVED_PAGE_FRAME_COUNT; frame < PAGE_FRAME_MAX_COUNT; frame++) {
        if (page_manager_state.page_frame_map[frame])
            continue;

        page_manager_state.page_frame_map[frame] = true;
        page_manager_state.free_page_frame_count--;
        return (int32_t) frame;
    }
    return -1;
}

static void release_page_frame(uint32_t frame) {
    if (frame >= KERNEL_RESERVED_PAGE_FRAME_COUNT &&
        frame < PAGE_FRAME_MAX_COUNT &&
        page_manager_state.page_frame_map[frame]) {
        page_manager_state.page_frame_map[frame] = false;
        page_manager_state.free_page_frame_count++;
    }
}

void *paging_allocate_kernel_page_frame(void) {
    int32_t frame = allocate_page_frame();
    return frame < 0 ? NULL : (void *) ((uint32_t) frame * PAGE_FRAME_SIZE);
}

void paging_free_kernel_page_frame(void *physical_addr) {
    release_page_frame((uint32_t) physical_addr / PAGE_FRAME_SIZE);
}

void paging_switch_page_directory(struct PageDirectory *page_dir) {
    if (page_dir == NULL)
        return;

    uint32_t physical_addr = (uint32_t) page_dir - KERNEL_VIRTUAL_ADDRESS_BASE;
    __asm__ volatile("mov %0, %%cr3" : : "r"(physical_addr) : "memory");
}

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    if (page_dir == NULL ||
        page_directory_index >= PAGE_ENTRY_COUNT ||
        page_directory_index == RECURSIVE_PAGE_DIRECTORY_INDEX)
        return NULL;

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];

    if (dir_entry->flag.present_bit) {
        if (dir_entry->flag.use_pagesize_4_mb)
            return NULL;
        return (struct PageTable *) (
            dir_entry->page_table_base_address * PAGE_FRAME_SIZE +
            KERNEL_VIRTUAL_ADDRESS_BASE
        );
    }

    int32_t frame = allocate_page_frame();
    if (frame < 0)
        return NULL;

    dir_entry->flag = (struct PageDirectoryEntryFlag) {
        .present_bit = true,
        .write_bit   = true,
        .us_bit      = page_directory_index < (KERNEL_VIRTUAL_ADDRESS_BASE >> 22),
    };
    dir_entry->page_table_base_address = (uint32_t) frame;

    struct PageTable *page_table = (struct PageTable *) (
        (uint32_t) frame * PAGE_FRAME_SIZE + KERNEL_VIRTUAL_ADDRESS_BASE
    );
    memset(page_table, 0, PAGE_FRAME_SIZE);
    if (page_dir == paging_get_current_page_directory_addr())
        flush_single_tlb((void *) (page_directory_index << 22));
    return page_table;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageTableEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t)virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t)virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table  = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return;
    page_table->table[page_table_index].flag = flag;
    page_table->table[page_table_index].physical_page_base_address = ((uint32_t) physical_addr >> 12);
    flush_single_tlb(virtual_addr);
}



/* --- Memory Management --- */

bool paging_allocate_check(uint32_t amount) {
    // Hitung jumlah frame (pembulatan ke atas) tanpa risiko overflow
    uint32_t frames_needed = amount / PAGE_FRAME_SIZE + (amount % PAGE_FRAME_SIZE != 0 ? 1u : 0u);
    return page_manager_state.free_page_frame_count >= frames_needed;
}

bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t vaddr = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);

    // Hanya untuk user space (bagian bawah), tidak boleh menyentuh higher half kernel / recursive mapping
    if (page_dir == NULL || vaddr >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;

    // Siapkan page table dulu (bisa memakai 1 frame sendiri)
    struct PageTable *page_table = paging_get_page_table(page_dir, vaddr >> 22);
    if (page_table == NULL)
        return false;

    // Sudah ter-map -> jangan timpa
    struct PageTableEntry *entry = &page_table->table[(vaddr >> 12) & 0x3FF];
    if (entry->flag.present_bit || entry->ignored == SWAP_PTE_MARKER)
        return false;

    int32_t frame = allocate_page_frame();
    if (frame < 0)
        return false;

    entry->flag = (struct PageTableEntryFlag) {
        .present_bit = true,
        .write_bit   = true,
        .us_bit      = true,
    };
    entry->physical_page_base_address = (uint32_t) frame;
    if (page_dir == paging_get_current_page_directory_addr()) {
        flush_single_tlb((void *) vaddr);
        memset((void *) vaddr, 0, PAGE_FRAME_SIZE);
    }
    return true;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t vaddr = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);
    if (page_dir == NULL || vaddr >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;

    // Jangan panggil paging_get_page_table() kalau PDE belum present (akan mengalokasikan page table baru)
    uint32_t page_directory_index = vaddr >> 22;
    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];
    if (!dir_entry->flag.present_bit || dir_entry->flag.use_pagesize_4_mb)
        return false;

    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL)
        return false;

    struct PageTableEntry *entry = &page_table->table[(vaddr >> 12) & 0x3FF];
    if (!entry->flag.present_bit) {
        if (entry->ignored != SWAP_PTE_MARKER)
            return false;
        swap_release_slot(entry->physical_page_base_address);
        memset(entry, 0, sizeof(struct PageTableEntry));
        return true;
    }

    release_page_frame(entry->physical_page_base_address);

    memset(entry, 0, sizeof(struct PageTableEntry));
    if (page_dir == paging_get_current_page_directory_addr())
        flush_single_tlb((void *) vaddr);
    return true;
}

bool paging_release_page_directory(struct PageDirectory *page_dir) {
    if (page_dir == NULL || page_dir == &_paging_kernel_page_directory ||
        page_dir == paging_get_current_page_directory_addr())
        return false;

    for (uint32_t directory_index = 0;
         directory_index < (KERNEL_VIRTUAL_ADDRESS_BASE >> 22);
         directory_index++) {
        struct PageDirectoryEntry *dir_entry = &page_dir->table[directory_index];
        if (!dir_entry->flag.present_bit || dir_entry->flag.use_pagesize_4_mb)
            continue;

        struct PageTable *page_table = paging_get_page_table(page_dir, directory_index);
        if (page_table == NULL)
            return false;

        for (uint32_t table_index = 0; table_index < PAGE_ENTRY_COUNT; table_index++) {
            struct PageTableEntry *entry = &page_table->table[table_index];
            if (entry->flag.present_bit) {
                release_page_frame(entry->physical_page_base_address);
            } else if (entry->ignored == SWAP_PTE_MARKER) {
                swap_release_slot(entry->physical_page_base_address);
            }
            memset(entry, 0, sizeof(*entry));
        }

        uint32_t table_frame = dir_entry->page_table_base_address;
        memset(dir_entry, 0, sizeof(*dir_entry));
        release_page_frame(table_frame);
    }

    uint32_t directory_frame = ((uint32_t) page_dir - KERNEL_VIRTUAL_ADDRESS_BASE) / PAGE_FRAME_SIZE;
    release_page_frame(directory_frame);
    return true;
}