/* Purpose: bounded M10-D4 test-only real task readiness and priority state.
 * Dependencies: kernel-priority-policy.h (pure D1 selector), stdbool/stdint.
 * Invariants: exactly four worker slots 0..3 and reserved supervisor slot15.
 * No heap, I/O, system calls, CPU context switching or state mutation in
 * immutable selection. Caller owns IF=0 exclusion on the boot CPU.
 * References: ADR-m10-d4-priority-supervisor-proof.md and AGENTS.md.
 */
#ifndef SPIFFOS_M10_D4_TASK_CONTROL_H
#define SPIFFOS_M10_D4_TASK_CONTROL_H
#include "kernel-priority-policy.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { D4_TASK_WORKERS=4, D4_TASK_SUPERVISOR=15, D4_TASK_CAPACITY=5 };
enum d4_task_status { D4_TASK_READY=1, D4_TASK_BLOCKED=2,
                      D4_TASK_QUARANTINED=3 };
enum d4_task_result { D4_TASK_OK, D4_TASK_BAD_ARGUMENT,
                      D4_TASK_BAD_STATE, D4_TASK_BAD_CALLER,
                      D4_TASK_NO_READY, D4_TASK_CORRUPT };

struct d4_task_entry {
    enum d4_task_status status;
    enum kernel_priority_class priority;
    uint64_t waiting_dispatches;
    uint64_t dispatch_count;
};
struct d4_task_control {
    struct d4_task_entry worker[D4_TASK_WORKERS];
    uint64_t supervisor_dispatches;
    uint64_t supervisor_wait;
    uint64_t decision_sequence;
    bool initialized;
};

/* Initialize an unused, zero-valued state owner; no frame or stack access. */
bool d4_task_init(struct d4_task_control *table);
/* Validate state identity and readiness invariants, with no mutations. */
bool d4_task_check(const struct d4_task_control *table);
/* Current worker alone may transition READY->BLOCKED. Caller must have
 * disabled IRQs; current_slot is the actual running worker identity. */
enum d4_task_result d4_task_block(struct d4_task_control *table,
                                 unsigned target, unsigned current_slot);
/* Supervisor-only BLOCKED->READY. Wake never changes saved frames. */
enum d4_task_result d4_task_wake(struct d4_task_control *table,
                                unsigned target, unsigned caller_slot);
/* Supervisor-only READY->QUARANTINED when a worker cannot be dispatched;
 * reject attempts to quarantine the running worker. */
enum d4_task_result d4_task_quarantine(struct d4_task_control *table,
                                     unsigned target, unsigned caller_slot,
                                     unsigned current_slot);
/* Supervisor-only QUARANTINED->READY after a separate frame validation.
 * A caller-provided false frame_valid must fail closed without mutation. */
enum d4_task_result d4_task_recover(struct d4_task_control *table,
                                   unsigned target, unsigned caller_slot,
                                   bool frame_valid);
/* Build a deterministic ready snapshot, including reserved supervisor;
 * no task state changes or implicit wakeups. */
enum d4_task_result d4_task_choose(const struct d4_task_control *table,
                                  uint32_t *slot);
/* Apply the one completed dispatch after IRQ-exit selection; saturating
 * ready-time age, no aging while blocked or quarantined; no wrap. */
enum d4_task_result d4_task_note_dispatch(struct d4_task_control *table,
                                          uint32_t selected_slot);
#endif
