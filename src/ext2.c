#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

// Instance variabel global 
struct EXT2Superblock superblock;
struct EXT2BlockGroupDescriptorTable bgd_table;

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

char *get_entry_name(void *entry) {
    return (char *)entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    return (uint16_t)((sizeof(struct EXT2DirectoryEntry) + name_len + 3u) & ~3u);
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)ptr;
    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(entry);
    return entry->rec_len + get_entry_record_len(dotdot->name_len);
}
// Fungsi pembantu pemetaan inode 
uint32_t inode_to_bgd(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) % INODES_PER_GROUP;
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    allocate_node_blocks(NULL, node, inode_to_bgd(inode));
    if (node->i_block[0] == 0) {
        return;
    }

    uint8_t buffer[BLOCK_SIZE] = {0};
    struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)buffer;
    dot->inode = inode;
    dot->rec_len = get_entry_record_len(1);
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(dot), ".", 1);

    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    dotdot->inode = parent_inode;
    dotdot->rec_len = BLOCK_SIZE - dot->rec_len;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(dotdot), "..", 2);

    write_blocks(buffer, node->i_block[0], 1);
}

// FS Initializer

bool is_empty_storage(void) {
    uint8_t boot_sector[BLOCK_SIZE];
    read_blocks(boot_sector, BOOT_SECTOR, 1);
    return (memcmp(boot_sector, fs_signature, BLOCK_SIZE) != 0);
}

void create_ext2(void) {
    // 1. Tulis boot sector 
    write_blocks(fs_signature, BOOT_SECTOR, 1);

    // 2. Setup Superblock
    memset(&superblock, 0, sizeof(struct EXT2Superblock));
    superblock.s_inodes_count      = INODES_PER_GROUP * GROUPS_COUNT;
    superblock.s_blocks_count      = BLOCKS_PER_GROUP * GROUPS_COUNT;
    superblock.s_r_blocks_count    = 0;
    superblock.s_free_blocks_count = superblock.s_blocks_count - 22; // 22 block pertama terpakai sistem + root
    superblock.s_free_inodes_count = superblock.s_inodes_count - 1;  // Root inode (1)
    superblock.s_first_data_block  = 1;
    superblock.s_first_ino         = 1;
    superblock.s_blocks_per_group  = BLOCKS_PER_GROUP;
    superblock.s_frags_per_group   = BLOCKS_PER_GROUP;
    superblock.s_inodes_per_group  = INODES_PER_GROUP;
    superblock.s_magic             = EXT2_SUPER_MAGIC;

    uint8_t sb_buf[BLOCK_SIZE] = {0};
    memcpy(sb_buf, &superblock, sizeof(struct EXT2Superblock));
    write_blocks(sb_buf, 1, 1);

    // 3. Setup Block Group Descriptor Table
    memset(&bgd_table, 0, sizeof(struct EXT2BlockGroupDescriptorTable));
    for (uint32_t i = 0; i < GROUPS_COUNT; i++) {
        uint32_t base_block = i * BLOCKS_PER_GROUP;
        bgd_table.table[i].bg_block_bitmap      = base_block + 3;
        bgd_table.table[i].bg_inode_bitmap      = base_block + 4;
        bgd_table.table[i].bg_inode_table       = base_block + 5;
        bgd_table.table[i].bg_free_blocks_count = BLOCKS_PER_GROUP;
        bgd_table.table[i].bg_free_inodes_count = INODES_PER_GROUP;
        bgd_table.table[i].bg_used_dirs_count   = 0;
    }

    // Group 0 memiliki root
    bgd_table.table[0].bg_free_blocks_count -= 22;
    bgd_table.table[0].bg_free_inodes_count -= 1;
    bgd_table.table[0].bg_used_dirs_count   = 1;

    uint8_t bgd_buf[BLOCK_SIZE] = {0};
    memcpy(bgd_buf, &bgd_table, sizeof(struct EXT2BlockGroupDescriptorTable));
    write_blocks(bgd_buf, 2, 1);

    // 4. Bitmaps Group 0
    uint8_t block_bitmap[BLOCK_SIZE] = {0};
    uint8_t inode_bitmap[BLOCK_SIZE] = {0};

    for (uint32_t i = 0; i < 22; i++) {
        block_bitmap[i / 8] |= (1 << (i % 8));
    }
    inode_bitmap[0] |= (1 << 0); // Inode 1 terpakai

    write_blocks(block_bitmap, bgd_table.table[0].bg_block_bitmap, 1);
    write_blocks(inode_bitmap, bgd_table.table[0].bg_inode_bitmap, 1);

    // 5. Inode Root (Inode 1)
    struct EXT2Inode root_inode;
    memset(&root_inode, 0, sizeof(struct EXT2Inode));
    root_inode.i_mode     = EXT2_S_IFDIR | EXT2_FT_DIR;
    root_inode.i_size     = BLOCK_SIZE;
    root_inode.i_blocks   = 1;
    root_inode.i_block[0] = bgd_table.table[0].bg_inode_table + INODES_TABLE_BLOCK_COUNT; // Block 21

    uint8_t inode_tb_buf[BLOCK_SIZE] = {0};
    memcpy(inode_tb_buf, &root_inode, sizeof(struct EXT2Inode));
    write_blocks(inode_tb_buf, bgd_table.table[0].bg_inode_table, 1);

    // 6. Data Block Root ("." & "..")
    uint8_t dir_data[BLOCK_SIZE] = {0};

    struct EXT2DirectoryEntry *entry_dot = (struct EXT2DirectoryEntry *)dir_data;
    entry_dot->inode     = 1;
    entry_dot->rec_len   = sizeof(struct EXT2DirectoryEntry) + 4; // Align 4 byte
    entry_dot->name_len  = 1;
    entry_dot->file_type = EXT2_FT_DIR;
    memcpy(dir_data + sizeof(struct EXT2DirectoryEntry), ".", 1);

    struct EXT2DirectoryEntry *entry_dotdot = (struct EXT2DirectoryEntry *)(dir_data + entry_dot->rec_len);
    entry_dotdot->inode     = 1;
    entry_dotdot->rec_len   = BLOCK_SIZE - entry_dot->rec_len;
    entry_dotdot->name_len  = 2;
    entry_dotdot->file_type = EXT2_FT_DIR;
    memcpy((uint8_t *)entry_dotdot + sizeof(struct EXT2DirectoryEntry), "..", 2);

    write_blocks(dir_data, root_inode.i_block[0], 1);
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        uint8_t sb_buf[BLOCK_SIZE];
        read_blocks(sb_buf, 1, 1);
        memcpy(&superblock, sb_buf, sizeof(struct EXT2Superblock));

        uint8_t bgd_buf[BLOCK_SIZE];
        read_blocks(bgd_buf, 2, 1);
        memcpy(&bgd_table, bgd_buf, sizeof(struct EXT2BlockGroupDescriptorTable));
    }
}

//read

void read_inode(uint32_t inode_num, struct EXT2Inode *out_inode) {
    if (inode_num == 0) return; // Inode 0 tidak valid di ext2

    uint32_t bgd_idx = inode_to_bgd(inode_num);
    uint32_t local_idx = inode_to_local(inode_num); // 0-indexed di dalam grup

    // Dapatkan block awal dari Inode Table untuk grup ini
    uint32_t inode_table_start = bgd_table.table[bgd_idx].bg_inode_table; //Dari yang sudah di initialize

    // Hitung offset blok dan offset byte di dalam blok
    uint32_t block_offset = local_idx / INODES_PER_TABLE;
    uint32_t byte_offset_in_block = (local_idx % INODES_PER_TABLE) * INODE_SIZE;

    // Baca blok disk yang berisi inode tersebut
    uint8_t buffer[BLOCK_SIZE];
    read_blocks(buffer, inode_table_start + block_offset, 1);

    // Salin data inode ke output struct
    memcpy(out_inode, buffer + byte_offset_in_block, INODE_SIZE);
}

void read_inode_content(struct EXT2Inode *inode, uint8_t *out_buffer, uint32_t buffer_size) {
    uint32_t bytes_read = 0;
    uint32_t current_block_idx = 0;
    
    // Batasi pembacaan agar tidak melewati ukuran asli file atau kapasitas buffer
    uint32_t max_read = (buffer_size < inode->i_size) ? buffer_size : inode->i_size;

    while (bytes_read < max_read) {
        uint32_t physical_block = 0;

        // a. Direct Blocks (0 - 11)
        if (current_block_idx < 12) {
            physical_block = inode->i_block[current_block_idx];
        } 
        // b. Singly Indirect Block (12)
        else if (current_block_idx < 12 + (BLOCK_SIZE / 4)) {
            uint32_t indirect_ptrs[BLOCK_SIZE / 4]; // 512 / 4 byte pointer = 128 pointer
            
            // Baca blok indirect dari disk terlebih dahulu
            if (inode->i_block[12] != 0) {
                read_blocks(indirect_ptrs, inode->i_block[12], 1);
                physical_block = indirect_ptrs[current_block_idx - 12];
            }
        }

        // Jika blok kosong (sparse file / end of file blocks)
        if (physical_block == 0) break; 

        // Baca blok data
        uint8_t block_buf[BLOCK_SIZE];
        read_blocks(block_buf, physical_block, 1);

        // Kalkulasi berapa byte yang harus disalin pada iterasi ini
        uint32_t bytes_to_copy = BLOCK_SIZE;
        if (max_read - bytes_read < BLOCK_SIZE) {
            bytes_to_copy = max_read - bytes_read;
        }

        // Salin ke buffer pemanggil
        memcpy(out_buffer + bytes_read, block_buf, bytes_to_copy);

        bytes_read += bytes_to_copy;
        current_block_idx++;
    }
}


uint32_t find_inode_by_name(uint32_t parent_inode_num, char *target_name, uint8_t name_len) {
    struct EXT2Inode parent_inode;
    read_inode(parent_inode_num, &parent_inode);

    // 1. Cek apakah parent adalah direktori (sudah disesuaikan dengan spesifikasi)
    bool is_parent_dir = (parent_inode.i_mode == EXT2_FT_DIR) || ((parent_inode.i_mode & EXT2_S_IFDIR) != 0);
    if (!is_parent_dir) return 0;

    // 2. Baca per block (512 byte) untuk menghindari VLA di stack
    uint32_t total_bytes_checked = 0;
    uint32_t block_idx = 0;

    while (total_bytes_checked < parent_inode.i_size) {
        uint8_t dir_block[BLOCK_SIZE]; // Ukuran buffer sekarang fixed 512 byte (Aman)
        
        // Dapatkan blok fisik disk
        uint32_t phys_block = 0;
        if (block_idx < 12) {
            phys_block = parent_inode.i_block[block_idx];
        } else if (parent_inode.i_block[12] != 0) {
            uint32_t indirect_ptrs[BLOCK_SIZE / sizeof(uint32_t)];
            read_blocks(indirect_ptrs, parent_inode.i_block[12], 1);
            phys_block = indirect_ptrs[block_idx - 12];
        }

        // Hentikan jika pointer blok kosong
        if (phys_block == 0) break;

        // Baca 1 blok saja ke buffer
        read_blocks(dir_block, phys_block, 1);

        // Cari entry di dalam blok yang sedang dibaca ini
        uint32_t offset = 0;
        while (offset < BLOCK_SIZE && (total_bytes_checked + offset) < parent_inode.i_size) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(dir_block + offset);

            // Berhenti jika entri kosong atau rusak
            if (entry->inode == 0 || entry->rec_len == 0) break;

            // Bandingkan panjang dan nama string
            if (entry->name_len == name_len) {
                char *entry_name = (char *)(dir_block + offset + sizeof(struct EXT2DirectoryEntry));
                if (memcmp(entry_name, target_name, name_len) == 0) {
                    return entry->inode; // File/Folder ditemukan!
                }
            }

            offset += entry->rec_len;
        }

        total_bytes_checked += BLOCK_SIZE;
        block_idx++;
    }

    return 0; // Tidak ditemukan
}


int8_t read_directory(struct EXT2DriverRequest *prequest) {
    struct EXT2Inode parent_inode;
    read_inode(prequest->parent_inode, &parent_inode);

    // [PERUBAHAN]: Pengecekan parent folder
    bool is_parent_dir = (parent_inode.i_mode == EXT2_FT_DIR) || ((parent_inode.i_mode & EXT2_S_IFDIR) != 0);
    if (!is_parent_dir) {
        return 3; // Parent folder invalid
    }

    uint32_t target_inode_num = 0;
    if (prequest->name_len == 0) {
        target_inode_num = prequest->parent_inode;
    } else {
        target_inode_num = find_inode_by_name(prequest->parent_inode, prequest->name, prequest->name_len);
        if (target_inode_num == 0) {
            return 2; // Folder tidak ditemukan
        }
    }

    struct EXT2Inode target_inode;
    read_inode(target_inode_num, &target_inode);

    // [PERUBAHAN]: Memastikan target ADALAH folder
    bool is_target_dir = (target_inode.i_mode == EXT2_FT_DIR) || ((target_inode.i_mode & EXT2_S_IFDIR) != 0);
    if (!is_target_dir) {
        return 1; // Bukan sebuah folder
    }

    read_inode_content(&target_inode, prequest->buf, prequest->buffer_size);
    return 0; // Success
}

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2Inode parent_inode;
    read_inode(request.parent_inode, &parent_inode);

    // [PERUBAHAN]: Pengecekan folder yang lebih ketat sesuai spesifikasi
    bool is_parent_dir = (parent_inode.i_mode == EXT2_FT_DIR) || ((parent_inode.i_mode & EXT2_S_IFDIR) != 0);
    if (!is_parent_dir) {
        return 4; // Parent folder invalid
    }

    uint32_t target_inode_num = find_inode_by_name(request.parent_inode, request.name, request.name_len);
    if (target_inode_num == 0) {
        return 3; // File tidak ditemukan
    }

    struct EXT2Inode target_inode;
    read_inode(target_inode_num, &target_inode);

    // [PERUBAHAN]: Memastikan target BUKAN folder (harus file reguler)
    bool is_target_dir = (target_inode.i_mode == EXT2_FT_DIR) || ((target_inode.i_mode & EXT2_S_IFDIR) != 0);
    if (is_target_dir) {
        return 1; // Bukan sebuah file (karena target adalah folder)
    }

    if (request.buffer_size < target_inode.i_size) {
        return 2; // Buffer tidak cukup
    }

    read_inode_content(&target_inode, request.buf, target_inode.i_size);
    return 0; // Success
}

static bool is_directory_inode(struct EXT2Inode *node)
{
    return node->i_mode == EXT2_FT_DIR || (node->i_mode & EXT2_S_IFDIR) != 0;
}

static bool is_dot_name(const char *name, uint8_t name_len)
{
    return (name_len == 1 && name[0] == '.') ||
           (name_len == 2 && name[0] == '.' && name[1] == '.');
}

static uint32_t get_directory_block(struct EXT2Inode *node, uint32_t index)
{
    if (index < 12)
        return node->i_block[index];
    if (index < 12 + BLOCK_SIZE / sizeof(uint32_t) && node->i_block[12] != 0) {
        uint32_t locations[BLOCK_SIZE / sizeof(uint32_t)];
        read_blocks(locations, node->i_block[12], 1);
        return locations[index - 12];
    }
    return 0;
}

static void commit_ext2_metadata(void)
{
    struct BlockBuffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    memcpy(&buffer, &superblock, sizeof(struct EXT2Superblock));
    write_blocks(&buffer, 1, 1);
    memset(&buffer, 0, sizeof(buffer));
    memcpy(&buffer, &bgd_table, sizeof(struct EXT2BlockGroupDescriptorTable));
    write_blocks(&buffer, 2, 1);
}

static void remove_directory_entry(uint8_t *block, uint32_t target)
{
    uint32_t previous = BLOCK_SIZE, last = 0, end = 0, offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block + offset);
        if (entry->inode == 0 || entry->rec_len == 0)
            break;
        if (offset < target)
            previous = offset;
        last = offset;
        offset += entry->rec_len;
        end = offset;
    }

    uint16_t rec_len = ((struct EXT2DirectoryEntry *)(block + target))->rec_len;
    if (end == BLOCK_SIZE && last == target && previous != BLOCK_SIZE) {
        ((struct EXT2DirectoryEntry *)(block + previous))->rec_len += rec_len;
        memset(block + target, 0, rec_len);
        return;
    }

    memmove(block + target, block + target + rec_len, end - target - rec_len);
    memset(block + end - rec_len, 0, rec_len);
    if (end == BLOCK_SIZE && last != target)
        ((struct EXT2DirectoryEntry *)(block + last - rec_len))->rec_len += rec_len;
}

bool is_directory_empty(uint32_t inode)
{
    struct EXT2Inode node;
    read_inode(inode, &node);
    for (uint32_t index = 0; index * BLOCK_SIZE < node.i_size; index++) {
        uint32_t location = get_directory_block(&node, index);
        if (location == 0)
            break;

        struct BlockBuffer block;
        read_blocks(&block, location, 1);
        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block.buf + offset);
            if (entry->inode == 0 || entry->rec_len == 0)
                break;
            if (!is_dot_name((char *)(entry + 1), entry->name_len))
                return false;
            offset += entry->rec_len;
        }
    }
    return true;
}

uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded)
{
    for (uint32_t i = 0; i < blocks; i++) {
        if (locations[i] == 0 || locations[i] >= superblock.s_blocks_count)
            continue;

        if (depth > 0) {
            uint32_t children[BLOCK_SIZE / sizeof(uint32_t)];
            read_blocks(children, locations[i], 1);
            deallocate_block(children, BLOCK_SIZE / sizeof(uint32_t), bitmap, depth - 1, last_bgd, bgd_loaded);
            bgd_loaded = *last_bgd < GROUPS_COUNT;
        }

        uint32_t bgd = locations[i] / BLOCKS_PER_GROUP;
        uint32_t local = locations[i] % BLOCKS_PER_GROUP;
        if (!bgd_loaded || *last_bgd != bgd) {
            if (bgd_loaded)
                write_blocks(bitmap, bgd_table.table[*last_bgd].bg_block_bitmap, 1);
            read_blocks(bitmap, bgd_table.table[bgd].bg_block_bitmap, 1);
            *last_bgd = bgd;
            bgd_loaded = true;
        }

        uint8_t mask = 1 << (local % 8);
        if (bitmap->buf[local / 8] & mask) {
            bitmap->buf[local / 8] &= ~mask;
            bgd_table.table[bgd].bg_free_blocks_count++;
            superblock.s_free_blocks_count++;
        }
    }
    return *last_bgd;
}

void deallocate_blocks(void *loc, uint32_t blocks)
{
    if (blocks == 0)
        return;

    uint32_t *locations = loc;
    struct BlockBuffer bitmap;
    uint32_t last_bgd = GROUPS_COUNT;
    deallocate_block(locations, 12, &bitmap, 0, &last_bgd, false);
    for (uint32_t depth = 1; depth <= 3; depth++)
        deallocate_block(locations + 11 + depth, 1, &bitmap, depth, &last_bgd, last_bgd < GROUPS_COUNT);

    if (last_bgd < GROUPS_COUNT)
        write_blocks(&bitmap, bgd_table.table[last_bgd].bg_block_bitmap, 1);
}

void deallocate_node(uint32_t inode)
{
    struct EXT2Inode node;
    read_inode(inode, &node);
    deallocate_blocks(node.i_block, node.i_blocks);

    uint32_t bgd = inode_to_bgd(inode);
    uint32_t local = inode_to_local(inode);
    struct BlockBuffer bitmap;
    read_blocks(&bitmap, bgd_table.table[bgd].bg_inode_bitmap, 1);

    uint8_t mask = 1 << (local % 8);
    if (bitmap.buf[local / 8] & mask) {
        bitmap.buf[local / 8] &= ~mask;
        write_blocks(&bitmap, bgd_table.table[bgd].bg_inode_bitmap, 1);
        bgd_table.table[bgd].bg_free_inodes_count++;
        superblock.s_free_inodes_count++;
        if (is_directory_inode(&node) && bgd_table.table[bgd].bg_used_dirs_count > 0)
            bgd_table.table[bgd].bg_used_dirs_count--;
    }
    commit_ext2_metadata();
}

int8_t delete(struct EXT2DriverRequest request)
{
    if (request.parent_inode == 0 || request.parent_inode > superblock.s_inodes_count)
        return 3;

    struct EXT2Inode parent;
    read_inode(request.parent_inode, &parent);
    if (!is_directory_inode(&parent))
        return 3;
    if (request.name_len == 0)
        return 1;
    if (is_dot_name(request.name, request.name_len))
        return -1;

    for (uint32_t index = 0; index * BLOCK_SIZE < parent.i_size; index++) {
        uint32_t location = get_directory_block(&parent, index);
        if (location == 0)
            break;

        struct BlockBuffer block;
        read_blocks(&block, location, 1);
        uint32_t offset = 0;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block.buf + offset);
            if (entry->inode == 0 || entry->rec_len == 0)
                break;

            if (entry->name_len == request.name_len &&
                memcmp(entry + 1, request.name, request.name_len) == 0) {
                uint32_t inode = entry->inode;
                struct EXT2Inode node;
                read_inode(inode, &node);
                if (is_directory_inode(&node) == request.is_directory) {
                    if (request.is_directory && !is_directory_empty(inode))
                        return 2;
                    remove_directory_entry(block.buf, offset);
                    write_blocks(&block, location, 1);
                    deallocate_node(inode);
                    return 0;
                }
            }
            offset += entry->rec_len;
        }
    }
    return 1;
}


static void write_filesystem_metadata(void) {
    uint8_t buffer[BLOCK_SIZE] = {0};
    memcpy(buffer, &superblock, sizeof(superblock));
    write_blocks(buffer, 1, 1);

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, &bgd_table, sizeof(bgd_table));
    write_blocks(buffer, 2, 1);
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    if (node == NULL || inode == 0 || inode > INODES_PER_GROUP * GROUPS_COUNT) {
        return;
    }

    uint32_t bgd_idx = inode_to_bgd(inode);
    uint32_t local_idx = inode_to_local(inode);
    uint32_t inode_offset = (local_idx % INODES_PER_TABLE) * INODE_SIZE;
    uint32_t inode_block = bgd_table.table[bgd_idx].bg_inode_table + local_idx / INODES_PER_TABLE;
    uint32_t byte_offset = inode_offset % BLOCK_SIZE;
    uint32_t first_part_size = BLOCK_SIZE - byte_offset;
    if (first_part_size > sizeof(*node)) {
        first_part_size = sizeof(*node);
    }

    uint8_t buffer[BLOCK_SIZE];
    read_blocks(buffer, inode_block, 1);
    memcpy(buffer + byte_offset, node, first_part_size);
    write_blocks(buffer, inode_block, 1);

    if (first_part_size < sizeof(*node)) {
        read_blocks(buffer, inode_block + 1, 1);
        memcpy(buffer, (uint8_t *)node + first_part_size,
               sizeof(*node) - first_part_size);
        write_blocks(buffer, inode_block + 1, 1);
    }
}

int8_t write(struct EXT2DriverRequest *request) {
    // 1. Validasi pointer dasar & nama (return -1 jika invalid)
    if (request == NULL || request->name == NULL || request->name_len == 0 ||
        (!request->is_directory && request->buffer_size > 0 && request->buf == NULL)) {
        return -1;
    }

    // 2. Validasi parent_inode (return 2 jika parent invalid/tidak ada/di luar batas)
    if (request->parent_inode == 0 || request->parent_inode > INODES_PER_GROUP * GROUPS_COUNT) {
        return 2;
    }

    struct EXT2Inode parent_node;
    read_inode(request->parent_inode, &parent_node);

    // 3. Validasi apakah parent memang sebuah direktori
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) {
        return 2;
    }
    if (parent_node.i_block[0] == 0) {
        return 2;
    }

    uint32_t existing_inode = find_inode_by_name(request->parent_inode, request->name, request->name_len);
    if (existing_inode != 0) {
        return 1;
    }

    uint32_t blocks_needed = request->buffer_size / BLOCK_SIZE;
    if (request->buffer_size % BLOCK_SIZE != 0) {
        blocks_needed++;
    }
    if (request->is_directory) {
        blocks_needed = 1;
    }
    uint32_t blocks_to_allocate = blocks_needed + (blocks_needed > 12 ? 1u : 0u);
    if (blocks_needed > 12 + BLOCK_SIZE / sizeof(uint32_t) ||
        blocks_to_allocate > bgd_table.table[0].bg_free_blocks_count) {
        return -1;
    }

    uint32_t new_inode_num = allocate_node();
    if (new_inode_num == 0) {
        return -1;
    }

    struct EXT2Inode new_node;
    memset(&new_node, 0, sizeof(struct EXT2Inode));

    uint32_t prefered_bgd = inode_to_bgd(new_inode_num);

    if (request->is_directory) {
        new_node.i_mode = EXT2_S_IFDIR;
        new_node.i_size = BLOCK_SIZE;
        new_node.i_blocks = 1;

        init_directory_table(&new_node, new_inode_num, request->parent_inode);

        bgd_table.table[prefered_bgd].bg_used_dirs_count++;
    } else {
        new_node.i_mode = EXT2_S_IFREG;
        new_node.i_size = request->buffer_size;

        new_node.i_blocks = blocks_needed + (blocks_needed > 12 ? 1u : 0u);

        if (blocks_needed > 0) {
            allocate_node_blocks(request->buf, &new_node, prefered_bgd);
            if (new_node.i_block[0] == 0) {
                return -1;
            }
        }
    }

    sync_node(&new_node, new_inode_num);

    uint8_t parent_buf[BLOCK_SIZE];
    read_inode_content(&parent_node, parent_buf, BLOCK_SIZE);

    uint16_t required_rec_len = get_entry_record_len(request->name_len);

    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)parent_buf;
    uint32_t offset = 0;
    bool entry_added = false;

    while (offset < BLOCK_SIZE && offset < parent_node.i_size) {
        uint16_t actual_len = get_entry_record_len(entry->name_len);
        if (entry->rec_len < actual_len || entry->rec_len > BLOCK_SIZE - offset) {
            return -1;
        }

        if (entry->rec_len - actual_len >= required_rec_len) {
            uint16_t old_rec_len = entry->rec_len;

            entry->rec_len = actual_len;

            struct EXT2DirectoryEntry *new_entry = (struct EXT2DirectoryEntry *)((uint8_t *)entry + actual_len);

            new_entry->inode = new_inode_num;
            new_entry->rec_len = old_rec_len - actual_len;
            new_entry->name_len = request->name_len;
            new_entry->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

            char *entry_name_ptr = get_entry_name(new_entry);
            memcpy(entry_name_ptr, request->name, request->name_len);

            entry_added = true;
            break;
        }

        offset += entry->rec_len;
        if (offset < parent_node.i_size) {
            entry = get_next_directory_entry(entry);
        }
    }

    if (!entry_added) {
        return -1;
    }

    write_blocks(parent_buf, parent_node.i_block[0], 1);
    sync_node(&parent_node, request->parent_inode);

    write_filesystem_metadata();

    return 0;
}

static bool allocate_data_block(uint32_t *block_num) {
    if (bgd_table.table[0].bg_free_blocks_count == 0 ||
        superblock.s_free_blocks_count == 0) {
        return false;
    }

    uint8_t bitmap[BLOCK_SIZE];
    uint32_t bitmap_block = bgd_table.table[0].bg_block_bitmap;
    read_blocks(bitmap, bitmap_block, 1);

    for (uint32_t bit = 0; bit < BLOCKS_PER_GROUP; bit++) {
        uint8_t mask = (uint8_t)(1u << (bit % 8));
        if ((bitmap[bit / 8] & mask) == 0) {
            bitmap[bit / 8] |= mask;
            write_blocks(bitmap, bitmap_block, 1);
            bgd_table.table[0].bg_free_blocks_count--;
            superblock.s_free_blocks_count--;
            *block_num = bit;
            write_filesystem_metadata();
            return true;
        }
    }

    return false;
}

uint32_t allocate_node(void) {
    if (bgd_table.table[0].bg_free_inodes_count == 0 ||
        superblock.s_free_inodes_count == 0) {
        return 0;
    }

    uint8_t bitmap[BLOCK_SIZE];
    uint32_t bitmap_block = bgd_table.table[0].bg_inode_bitmap;
    read_blocks(bitmap, bitmap_block, 1);

    for (uint32_t bit = 0; bit < INODES_PER_GROUP; bit++) {
        uint8_t mask = (uint8_t)(1u << (bit % 8));
        if ((bitmap[bit / 8] & mask) == 0) {
            bitmap[bit / 8] |= mask;
            write_blocks(bitmap, bitmap_block, 1);
            bgd_table.table[0].bg_free_inodes_count--;
            superblock.s_free_inodes_count--;
            write_filesystem_metadata();
            return bit + 1;
        }
    }

    return 0;
}

void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    (void)prefered_bgd;

    uint32_t blocks_needed = node->i_size / BLOCK_SIZE;
    if (node->i_size % BLOCK_SIZE != 0) {
        blocks_needed++;
    }
    if (blocks_needed > 12 + BLOCK_SIZE / sizeof(uint32_t)) {
        return;
    }
    uint32_t total_blocks = blocks_needed + (blocks_needed > 12 ? 1u : 0u);
    if (total_blocks > bgd_table.table[0].bg_free_blocks_count) {
        return;
    }

    uint32_t indirect[BLOCK_SIZE / sizeof(uint32_t)] = {0};
    uint32_t indirect_block = 0;
    if (blocks_needed > 12 && !allocate_data_block(&indirect_block)) {
        return;
    }

    uint8_t block_buffer[BLOCK_SIZE];
    uint32_t bytes_written = 0;
    for (uint32_t i = 0; i < blocks_needed; i++) {
        uint32_t data_block;
        if (!allocate_data_block(&data_block)) {
            return;
        }

        if (i < 12) {
            node->i_block[i] = data_block;
        } else {
            indirect[i - 12] = data_block;
        }

        memset(block_buffer, 0, sizeof(block_buffer));
        uint32_t remaining = node->i_size - bytes_written;
        uint32_t copy_size = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        if (ptr != NULL && copy_size != 0) {
            memcpy(block_buffer, (uint8_t *)ptr + bytes_written, copy_size);
        }
        write_blocks(block_buffer, data_block, 1);
        bytes_written += copy_size;
    }

    if (blocks_needed > 12) {
        node->i_block[12] = indirect_block;
        write_blocks(indirect, indirect_block, 1);
    }
}