/* Purpose: pure, single-CPU M10-D1 priority selection (no switching).
 * Responsibilities: four priorities, dispatch-count aging, reserved service
 * for the supervisor, deterministic tie-breaking and strict input validation.
 * Dependencies: stdbool/stdint; no interrupts, heap, task ownership or IO.
 * Invariants: supervisor can only occupy the reserved slot; candidate slots
 * are unique; errors preserve caller output; caller owns wait-age counters.
 * Reference: docs/decisions/ADR-m10-d-preemption-proposal.md.
 */
#ifndef SPIFFOS_KERNEL_PRIORITY_POLICY_H
#define SPIFFOS_KERNEL_PRIORITY_POLICY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    KERNEL_PRIORITY_TASK_CAPACITY = 16,
    KERNEL_PRIORITY_SUPERVISOR_SLOT = 15,
    KERNEL_PRIORITY_AGE_WEIGHT = 8,
    KERNEL_PRIORITY_SUPERVISOR_INTERVAL = 4
};

enum kernel_priority_class {
    KERNEL_PRIORITY_BACKGROUND = 0,
    KERNEL_PRIORITY_NORMAL = 1,
    KERNEL_PRIORITY_INTERACTIVE = 2,
    KERNEL_PRIORITY_TIME_SENSITIVE = 3
};

enum kernel_priority_result {
    KERNEL_PRIORITY_OK,
    KERNEL_PRIORITY_INVALID,
    KERNEL_PRIORITY_EMPTY
};

/* Input is a snapshot of runnable tasks only. waiting_dispatches counts
 * completed dispatch decisions since each task last ran, maintained by
 * the caller (saturating at UINT64_MAX). The supervisor has a reserved
 * descriptor and independent stack that this selector does not allocate. */
struct kernel_priority_candidate {
    uint32_t slot;
    enum kernel_priority_class priority;
    uint64_t waiting_dispatches;
    bool supervisor;
};

/* Choose exactly one slot without mutating inputs or output on failure.
 * dispatch_sequence is the number of preceding dispatch decisions, zero
 * based; every fourth dispatch is reserved for the supervisor if runnable.
 * Otherwise use saturating score = wait_age + priority*8, ties resolved by
 * greater wait age and then lower slot. Every continually ready candidate
 * eventually outranks newly requeued CPU hogs. No IRQ-context calls allowed;
 * caller owns serialization and validates task membership separately.
 * The D4 isolated single-CPU IRQ-exit diagnostic may call this pure bounded
 * routine with IF=0, after the normal IRQ handler has sent its one EOI.
 * General-purpose IRQ handlers must NOT invoke it or change task queues.
 */
enum kernel_priority_result kernel_priority_choose(
    const struct kernel_priority_candidate *candidates, size_t count,
    uint64_t dispatch_sequence, uint32_t *selected_slot);
#endif
