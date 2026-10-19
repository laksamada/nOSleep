#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

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