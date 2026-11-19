#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"
#include "header/memory/swap.h"
#include "header/process/syscall.h"

#define PROCESS_NAME_LENGTH_MAX          32
#define PROCESS_COUNT_MAX                16
#define PROCESS_IMAGE_SIZE_MAX           0x200000u
#define PROCESS_USER_STACK_TOP           (USER_PROGRAM_MEMORY_LIMIT - 4)

#define CPU_EFLAGS_BASE_FLAG               0x2
#define CPU_EFLAGS_FLAG_INTERRUPT_ENABLE   0x200

#define PROCESS_CREATE_SUCCESS                   0
#define PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED 1
#define PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT   2
#define PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY    3
#define PROCESS_CREATE_FAIL_FS_READ_FAILURE      4

enum ProcessState {
    PROCESS_INACTIVE,
    PROCESS_READY,
    PROCESS_RUNNING,
};

/**
 * Context, CPU state yang disimpan saat proses ditinggalkan scheduler
 *
 * @param cpu    Register umum, index, stack, dan segment
 * @param eip    Alamat instruksi berikutnya
 * @param eflags Flag CPU
 * @param page_directory_virtual_addr Page directory milik proses
 */
struct Context {
    struct CPURegister    cpu;
    uint32_t              eip;
    uint32_t              eflags;
    struct PageDirectory *page_directory_virtual_addr;
} __attribute__((packed));

struct ProcessControlBlock {
    struct {
        uint32_t          pid;
        char              name[PROCESS_NAME_LENGTH_MAX];
        enum ProcessState state;
    } metadata;

    struct Context context;

    struct {
        uint32_t image_size;
    } memory;
};

struct ProcessManagerState {
    uint32_t active_process_count;
    int32_t  current_index;
    uint32_t next_pid;
};

extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];
extern struct ProcessManagerState process_manager_state;

/**
 * Get currently running process PCB pointer
 *
 * @return Will return NULL if there's no running process
 */
struct ProcessControlBlock* process_get_current_running_pcb_pointer(void);

/**
 * Create new user process and setup the virtual address space.
 * Executable dicari di folder request.parent_inode pada file system EXT2.
 *
 * @param request Appropriate read request for the executable
 * @param pid     Diisi PID proses baru jika berhasil
 * @return        Process creation return code (PROCESS_CREATE_*)
 */
int32_t process_create_user_process(struct EXT2DriverRequest request, uint32_t *pid);

/**
 * Destroy process then release page directory and process control block
 *
 * @param pid Process ID to delete
 * @return    True if process destruction success
 */
bool process_destroy(uint32_t pid);

// Cari PCB berdasarkan PID, NULL jika tidak ada
struct ProcessControlBlock* process_find(uint32_t pid);

// Salin daftar proses aktif ke buffer, kembalikan jumlah yang disalin
uint32_t process_list_info(struct ProcessInfo *out, uint32_t max_count);

#endif
