/* Purpose: D5.4 policy adapter for authoritative M10-A dispatch.
 * Responsibilities: collect live generation-keyed ready priorities, preserve
 * bounded aging and supervisor reserve, invoke M10-A selected dispatch.
 * Dependencies: kernel-sync-d53 and kernel-priority-policy.
 * Invariants: single authoritative task state and FIFO queue, no raw task
 * pointers, no IRQ scheduling or allocations, no invented supervisor.
 * Concurrency: one CPU; caller holds IF=0 for all operations.
 * Reference: ADR-m10-d54-priority-dispatch.md and AGENTS.md. */
#ifndef SPIFFOS_KERNEL_DISPATCH_D54_H
#define SPIFFOS_KERNEL_DISPATCH_D54_H
#include "kernel-sync-d53.h"

struct d54_dispatch {
    bool initialized;
    uint64_t decisions;
    uint64_t age[KERNEL_TASK_CAPACITY];
    struct kernel_task_handle generation[KERNEL_TASK_CAPACITY];
};

/* Initialize caller-zeroed policy ages, preserving M10-A task authority. */
bool d54_init(struct d54_dispatch *scheduler);

/* Select from M10-A's exact current ready queue using D5.3 effective
 * priorities. Supervisor gets an independent reserved dispatch opportunity
 * once every four decisions if currently RUNNABLE. Task state changes only
 * inside kernel_task_dispatch_selected. Reject invalid input unchanged. */
enum kernel_task_result d54_dispatch_next(
    struct d54_dispatch *scheduler,
    const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    struct kernel_task_table *tasks,
    struct kernel_task_handle *out);
#endif
