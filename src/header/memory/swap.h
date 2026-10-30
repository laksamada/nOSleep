#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"

#define PAGE_FRAME_ON_SWAP_MAX_COUNT 512
#define SWAP_BLOCKS_PER_PAGE         8
#define SWAP_START_BLOCK             (8192u - PAGE_FRAME_ON_SWAP_MAX_COUNT * SWAP_BLOCKS_PER_PAGE)
#define SWAP_PTE_MARKER              0b001
#define USER_PROGRAM_MEMORY_LIMIT    0x400000u

struct SwapManager {
    bool     swap_used[PAGE_FRAME_ON_SWAP_MAX_COUNT];
    uint32_t free_count;
} __attribute__((packed));

extern struct SwapManager swap_manager;

/**
 * Read and put frame back to memory
 * @param page_dir        Page directory to read
 * @param virtual_addr    Virtual Address that caused page fault
 */
bool swap_in(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * write frame to swap
 * @param page_dir        Page directory to read
 * @param virtual_addr    Virtual Address that caused page fault
 */
void swap_out(struct PageDirectory *page_dir, void *virtual_addr);

void swap_release_slot(uint32_t slot);

/**
 * read page fault address from cr2
 */
uint32_t read_cr2(void);

/**
 * handle page fault
 * @param error_code error code read from cpu state
 * make sure to handle every cases including page replacement
 */
void handle_page_fault(uint32_t error_code);

#endif
