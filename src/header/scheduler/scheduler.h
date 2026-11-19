#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "header/process/process.h"

#define PIT_TIMER_FREQUENCY      1000
#define SCHEDULER_QUANTUM_TICKS  10

/**
 * Read all general purpose register values and set control register.
 * Resume the execution flow back to ctx.eip and ctx.eflags
 *
 * @note          Implemented in assembly
 * @param context Target context to switch into
 */
__attribute__((noreturn)) extern void process_context_switch(struct Context ctx);

/**
 * Initialize scheduler before executing init process, menyalakan PIT pada PIT_TIMER_FREQUENCY Hz
 */
void scheduler_init(void);

/**
 * Save context to current running process
 *
 * @param ctx Context to save to current running process control block
 */
void scheduler_save_context_to_current_running_pcb(struct Context ctx);

/**
 * Trigger the scheduler algorithm (round robin) and context switch to new process
 */
__attribute__((noreturn)) void scheduler_switch_to_next_process(void);

// Simpan state proses yang sedang berjalan dari interrupt frame, lalu pindah ke proses berikutnya
__attribute__((noreturn)) void scheduler_yield(struct InterruptFrame frame);

// Dipanggil setiap IRQ timer, melakukan preemption jika jatah waktu proses user habis
void scheduler_handle_timer(struct InterruptFrame frame);

// Jumlah tick timer sejak scheduler_init (1 tick = 1 ms)
uint32_t scheduler_get_ticks(void);

#endif
