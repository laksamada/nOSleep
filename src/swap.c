#include "header/memory/swap.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"

_Static_assert(SWAP_START_BLOCK >= BLOCKS_PER_GROUP * GROUPS_COUNT, "Swap space overlaps file system blocks");
_Static_assert(SWAP_BLOCKS_PER_PAGE * BLOCK_SIZE == PAGE_FRAME_SIZE, "Swap slot must hold exactly one page");

struct SwapManager swap_manager = {
    .free_count = PAGE_FRAME_ON_SWAP_MAX_COUNT,
};

static uint32_t clock_hand = 0;

uint32_t read_cr2(void) {
    uint32_t fault_address;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));
    return fault_address;
}

static struct PageTableEntry *get_page_table_entry(struct PageDirectory *page_dir, uint32_t vaddr) {
    uint32_t page_directory_index = vaddr >> 22;
    if (!page_dir->table[page_directory_index].flag.present_bit)
        return NULL;

    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL)
        return NULL;
    return &page_table->table[(vaddr >> 12) & 0x3FF];
}

static uint32_t swap_slot_block(uint32_t slot) {
    return SWAP_START_BLOCK + slot * SWAP_BLOCKS_PER_PAGE;
}

void swap_release_slot(uint32_t slot) {
    if (slot < PAGE_FRAME_ON_SWAP_MAX_COUNT && swap_manager.swap_used[slot]) {
        swap_manager.swap_used[slot] = false;
        swap_manager.free_count++;
    }
}

bool swap_in(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t vaddr = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);
    struct PageTableEntry *entry = get_page_table_entry(page_dir, vaddr);
    if (entry == NULL || entry->flag.present_bit || entry->ignored != SWAP_PTE_MARKER)
        return false;

    uint32_t slot = entry->physical_page_base_address;
    if (!paging_allocate_user_page_frame(page_dir, (void *) vaddr))
        return false;

    entry->ignored = 0;
    read_blocks((void *) vaddr, swap_slot_block(slot), SWAP_BLOCKS_PER_PAGE);
    swap_release_slot(slot);
    return true;
}

void swap_out(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t vaddr = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);
    struct PageTableEntry *entry = get_page_table_entry(page_dir, vaddr);
    if (entry == NULL || !entry->flag.present_bit || swap_manager.free_count == 0)
        return;

    uint32_t slot = 0;
    while (swap_manager.swap_used[slot])
        slot++;

    write_blocks((void *) vaddr, swap_slot_block(slot), SWAP_BLOCKS_PER_PAGE);
    swap_manager.swap_used[slot] = true;
    swap_manager.free_count--;

    uint32_t frame = entry->physical_page_base_address;
    page_manager_state.page_frame_map[frame] = false;
    page_manager_state.free_page_frame_count++;

    memset(entry, 0, sizeof(struct PageTableEntry));
    entry->ignored = SWAP_PTE_MARKER;
    entry->physical_page_base_address = slot;
    flush_single_tlb((void *) vaddr);
}

static bool evict_page(struct PageDirectory *page_dir) {
    uint32_t page_count = KERNEL_VIRTUAL_ADDRESS_BASE / PAGE_FRAME_SIZE;
    if (swap_manager.free_count == 0)
        return false;

    for (uint32_t scanned = 0; scanned < 2 * page_count; scanned++) {
        uint32_t page_directory_index = clock_hand >> 10;
        if (!page_dir->table[page_directory_index].flag.present_bit) {
            clock_hand = ((page_directory_index + 1) << 10) % page_count;
            continue;
        }

        uint32_t vaddr = clock_hand * PAGE_FRAME_SIZE;
        clock_hand = (clock_hand + 1) % page_count;

        struct PageTableEntry *entry = get_page_table_entry(page_dir, vaddr);
        if (entry == NULL || !entry->flag.present_bit)
            continue;

        if (entry->flag.accessed_bit) {
            entry->flag.accessed_bit = false;
            flush_single_tlb((void *) vaddr);
            continue;
        }

        swap_out(page_dir, (void *) vaddr);
        return true;
    }
    return false;
}

static void print_hex(uint32_t value) {
    const char *digits = "0123456789ABCDEF";
    framebuffer_putchar('0', 0xC, 0);
    framebuffer_putchar('x', 0xC, 0);
    for (int shift = 28; shift >= 0; shift -= 4)
        framebuffer_putchar(digits[(value >> shift) & 0xF], 0xC, 0);
}

static void print_text(const char *text) {
    while (*text != '\0')
        framebuffer_putchar(*text++, 0xC, 0);
}

static void page_fault_panic(const char *reason, uint32_t fault_address, uint32_t error_code) {
    print_text("\nPAGE FAULT: ");
    print_text(reason);
    print_text("\naddress ");
    print_hex(fault_address);
    print_text(" error ");
    print_hex(error_code);
    while (true)
        __asm__ volatile("cli; hlt");
}

void handle_page_fault(uint32_t error_code) {
    uint32_t fault_address = read_cr2();
    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();

    if (error_code & 0x1)
        page_fault_panic("protection violation", fault_address, error_code);
    if (fault_address >= USER_PROGRAM_MEMORY_LIMIT)
        page_fault_panic("address outside user program", fault_address, error_code);

    while (page_manager_state.free_page_frame_count < 2)
        if (!evict_page(page_dir))
            page_fault_panic("out of memory and swap", fault_address, error_code);

    if (swap_in(page_dir, (void *) fault_address))
        return;
    if (!paging_allocate_user_page_frame(page_dir, (void *) fault_address))
        page_fault_panic("cannot allocate page frame", fault_address, error_code);
}
