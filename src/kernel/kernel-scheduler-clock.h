/* Purpose: M10-C bounded scheduler tick accounting and cooperative request flag.
 * Dependencies: C11 integers and x86-64 single-CPU lock-free atomics.
 * Invariants: no actual switching or task queue work in IRQ0, no IO/heap;
 * clock saturates without wrap and coalesces outstanding reschedule requests.
 * Reader may take one atomic request only at a safe non-IRQ boundary.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#ifndef SPIFFOS_KERNEL_SCHEDULER_CLOCK_H
#define SPIFFOS_KERNEL_SCHEDULER_CLOCK_H
#include <stdbool.h>
#include <stdint.h>

/* Only the IRQ writer advances tick_count and phase_ticks. The pending
 * request is one coalescing bit, not the number of runnable tasks or a
 * promise of immediate preemption. No priority policy is decided here. */
struct kernel_scheduler_clock {
    uint64_t tick_count;
    uint32_t phase_ticks;
    uint32_t request_interval_ticks;
    bool reschedule_pending;
    bool initialized;
};

/* Initialize exactly once with a positive interval (ticks, not millis).
 * Exclusive boot CPU and IF=0; failure does not modify caller state. */
bool kernel_scheduler_clock_init(struct kernel_scheduler_clock *clock,
                                 uint32_t interval_ticks);

/* Single CPU IRQ0 increments a saturating counter and records a coalesced
 * request each interval. No memory allocation, locks, IO or context switch. */
bool kernel_scheduler_clock_note_tick(struct kernel_scheduler_clock *clock);

/* Lock-free snapshot for non-IRQ callers. Failure preserves output. */
bool kernel_scheduler_clock_read_ticks(
    const struct kernel_scheduler_clock *clock, uint64_t *out);

/* Clear and return the pending request at a safe dispatcher boundary.
 * Atomic exchange avoids losing a request when interrupted by IRQ0.
 * Not intended for IRQ-context consumption. */
bool kernel_scheduler_clock_take_request(struct kernel_scheduler_clock *clock);

/* One static boot-CPU adapter. Initialization occurs before IF=1; IRQ0
 * calls note_tick, and the polled loop may consume requests without any
 * preemptive scheduling. */
bool kernel_scheduler_clock_boot_init(uint32_t interval_ticks);
bool kernel_scheduler_clock_boot_note_tick(void);
bool kernel_scheduler_clock_boot_take_request(void);
bool kernel_scheduler_clock_boot_read_ticks(uint64_t *out);

#endif
