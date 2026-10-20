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

// Fungsi pembantu pemetaan inode 
uint32_t inode_to_bgd(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    if (inode == 0) return 0;
    return (inode - 1) % INODES_PER_GROUP;
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
    uint32_t block_offset = (local_idx * INODE_SIZE) / BLOCK_SIZE;
    uint32_t byte_offset_in_block = (local_idx * INODE_SIZE) % BLOCK_SIZE;

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