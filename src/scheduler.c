#include "header/scheduler/scheduler.h"
#include "header/cpu/portio.h"
#include "header/text/framebuffer.h"

#define PIT_COMMAND_PORT   0x43
#define PIT_CHANNEL0_PORT  0x40
#define PIT_BASE_FREQUENCY 1193182
#define PIT_MODE_SQUARE    0x36

static uint32_t timer_ticks;
static uint32_t quantum_left = SCHEDULER_QUANTUM_TICKS;

void scheduler_init(void) {
    uint16_t divisor = PIT_BASE_FREQUENCY / PIT_TIMER_FREQUENCY;
    out(PIT_COMMAND_PORT, PIT_MODE_SQUARE);
    out(PIT_CHANNEL0_PORT, divisor & 0xFF);
    out(PIT_CHANNEL0_PORT, divisor >> 8);
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}

uint32_t scheduler_get_ticks(void) {
    return timer_ticks;
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    if (pcb != NULL)
        pcb->context = ctx;
}

void scheduler_switch_to_next_process(void) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL)
        current->metadata.state = PROCESS_READY;

    int32_t start = process_manager_state.current_index;
    for (int32_t step = 1; step <= PROCESS_COUNT_MAX; step++) {
        int32_t index = (start + step) % PROCESS_COUNT_MAX;
        struct ProcessControlBlock *next = &_process_list[index];
        if (next->metadata.state == PROCESS_INACTIVE)
            continue;

        next->metadata.state = PROCESS_RUNNING;
        process_manager_state.current_index = index;
        quantum_left = SCHEDULER_QUANTUM_TICKS;
        paging_use_page_directory(next->context.page_directory_virtual_addr);
        process_context_switch(next->context);
    }

    puts("\nNo process left to run\n", 24, 0xC);
    while (true)
        __asm__ volatile("hlt");
}

void scheduler_yield(struct InterruptFrame frame) {
    struct Context ctx = {
        .cpu    = frame.cpu,
        .eip    = frame.int_stack.eip,
        .eflags = frame.int_stack.eflags,
        .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
    };
    scheduler_save_context_to_current_running_pcb(ctx);
    scheduler_switch_to_next_process();
}

void scheduler_handle_timer(struct InterruptFrame frame) {
    timer_ticks++;
    pic_ack(IRQ_TIMER);

    bool from_user = (frame.int_stack.cs & 0x3) == 0x3;
    if (!from_user || --quantum_left > 0)
        return;
    scheduler_yield(frame);
}
