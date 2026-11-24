#include "header/scheduler/scheduler.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/memory/paging.h"
#include "header/process/process.h"

#define PIT_INPUT_FREQUENCY 1193182u
#define SCHEDULER_TICK_RATE 100u

static uint32_t last_scheduled_pid;

void scheduler_init(void) {
    uint16_t divisor = (uint16_t) (PIT_INPUT_FREQUENCY / SCHEDULER_TICK_RATE);
    out(0x43, 0x36);
    out(0x40, (uint8_t) divisor);
    out(0x40, (uint8_t) (divisor >> 8));

    out(PIC1_DATA, in(PIC1_DATA) & ~(1u << IRQ_TIMER));
    last_scheduled_pid = 0;
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    if (pcb == NULL)
        return;

    pcb->context = ctx;
    pcb->metadata.state = PROCESS_STATE_READY;
    last_scheduled_pid = pcb->metadata.pid;
    process_set_current_running_pcb(NULL);
}

static struct ProcessControlBlock *find_next_ready_process(void) {
    struct ProcessControlBlock *next_after_last = NULL;
    struct ProcessControlBlock *first_ready = NULL;

    for (uint32_t slot = 0; slot < PROCESS_COUNT_MAX; slot++) {
        struct ProcessControlBlock *pcb = process_get_slot(slot);
        if (pcb == NULL || pcb->metadata.state != PROCESS_STATE_READY)
            continue;

        if (first_ready == NULL || pcb->metadata.pid < first_ready->metadata.pid)
            first_ready = pcb;
        if (pcb->metadata.pid > last_scheduled_pid &&
            (next_after_last == NULL || pcb->metadata.pid < next_after_last->metadata.pid))
            next_after_last = pcb;
    }

    return next_after_last != NULL ? next_after_last : first_ready;
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    __asm__ volatile("cli");

    struct ProcessControlBlock *next = find_next_ready_process();
    if (next == NULL) {
        process_set_current_running_pcb(NULL);
        paging_switch_page_directory(&_paging_kernel_page_directory);
        while (true)
            __asm__ volatile("sti; hlt; cli");
    }

    last_scheduled_pid = next->metadata.pid;
    process_set_current_running_pcb(next);
    paging_switch_page_directory(&_paging_kernel_page_directory);
    process_context_switch(&next->context);
}
