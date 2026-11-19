#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

struct ProcessManagerState process_manager_state = {
    .active_process_count = 0,
    .current_index        = -1,
    .next_pid             = 1,
};

static uint32_t ceil_div(uint32_t numerator, uint32_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (_process_list[i].metadata.state == PROCESS_INACTIVE)
            return i;
    return -1;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    int32_t index = process_manager_state.current_index;
    if (index < 0 || _process_list[index].metadata.state == PROCESS_INACTIVE)
        return NULL;
    return &_process_list[index];
}

struct ProcessControlBlock* process_find(uint32_t pid) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        if (_process_list[i].metadata.state != PROCESS_INACTIVE && _process_list[i].metadata.pid == pid)
            return &_process_list[i];
    return NULL;
}

static bool find_executable(struct EXT2DriverRequest *request, uint32_t *size) {
    uint32_t inode = find_inode_by_name(request->parent_inode, request->name, request->name_len);
    if (inode == 0)
        return false;

    struct EXT2Inode node;
    read_inode(inode, &node);
    *size = node.i_size;
    return (node.i_mode & EXT2_S_IFDIR) == 0;
}

int32_t process_create_user_process(struct EXT2DriverRequest request, uint32_t *pid) {
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX)
        return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
    if (request.name_len == 0 || request.name_len >= PROCESS_NAME_LENGTH_MAX)
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;

    char name[PROCESS_NAME_LENGTH_MAX] = {0};
    memcpy(name, request.name, request.name_len);
    request.name = name;

    uint32_t size;
    if (!find_executable(&request, &size) || size == 0)
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;

    uint32_t page_frame_count_needed = ceil_div(size + PAGE_FRAME_SIZE, PAGE_FRAME_SIZE);
    if (size > request.buffer_size || !paging_allocate_check(page_frame_count_needed * PAGE_FRAME_SIZE))
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    struct PageDirectory *page_dir = paging_create_new_page_directory();
    if (page_dir == NULL)
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    struct PageDirectory *previous = paging_get_current_page_directory_addr();
    paging_use_page_directory(page_dir);
    request.buffer_size = size;
    bool loaded = read(request) == 0;
    paging_use_page_directory(previous);

    if (!loaded) {
        paging_free_page_directory(page_dir);
        return PROCESS_CREATE_FAIL_FS_READ_FAILURE;
    }

    struct ProcessControlBlock *new_pcb = &_process_list[process_list_get_inactive_index()];
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
    new_pcb->metadata.pid   = process_manager_state.next_pid++;
    new_pcb->metadata.state = PROCESS_READY;
    memcpy(new_pcb->metadata.name, name, PROCESS_NAME_LENGTH_MAX);

    uint32_t user_data = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.segment.ds = user_data;
    new_pcb->context.cpu.segment.es = user_data;
    new_pcb->context.cpu.segment.fs = user_data;
    new_pcb->context.cpu.segment.gs = user_data;
    new_pcb->context.cpu.stack.esp  = PROCESS_USER_STACK_TOP;
    new_pcb->context.eip            = (uint32_t) request.buf;
    new_pcb->context.eflags         = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = page_dir;
    new_pcb->memory.image_size      = size;

    process_manager_state.active_process_count++;
    *pid = new_pcb->metadata.pid;
    return PROCESS_CREATE_SUCCESS;
}

bool process_destroy(uint32_t pid) {
    struct ProcessControlBlock *pcb = process_find(pid);
    if (pcb == NULL)
        return false;

    paging_free_page_directory(pcb->context.page_directory_virtual_addr);
    memset(pcb, 0, sizeof(struct ProcessControlBlock));
    process_manager_state.active_process_count--;
    return true;
}

uint32_t process_list_info(struct ProcessInfo *out, uint32_t max_count) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX && count < max_count; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state == PROCESS_INACTIVE)
            continue;
        uint32_t frames = paging_count_user_frames(pcb->context.page_directory_virtual_addr);
        out[count].pid         = pcb->metadata.pid;
        out[count].state       = pcb->metadata.state;
        out[count].frame_count = frames;
        memcpy(out[count].name, pcb->metadata.name, PROCESS_NAME_LENGTH_MAX);
        count++;
    }
    return count;
}
