/* Purpose: M10-D5.2c bounded supervisor task-cleanup FIFO.
 * Responsibilities: queue terminated full-generation task handles, preserve
 * blocked owners, and delegate safe stack retirement to D5.2b registry.
 * Dependencies: kernel-task-resources.h; no heap, host OS or hardware I/O.
 * Invariants: one entry per task generation, no stale owners, no silent
 * drop of blocked entries, fail-closed quarantine after release failure.
 * Concurrency: single boot CPU, IF=0 throughout; never call from IRQ.
 * References: ADR-m10-d5-supervisor-cleanup.md and AGENTS.md. */
#ifndef SPIFFOS_KERNEL_TASK_CLEANUP_H
#define SPIFFOS_KERNEL_TASK_CLEANUP_H
#include "kernel-task-resources.h"

enum kernel_cleanup_result {
    KERNEL_CLEANUP_OK,
    KERNEL_CLEANUP_EMPTY,
    KERNEL_CLEANUP_BLOCKED,
    KERNEL_CLEANUP_QUARANTINED,
    KERNEL_CLEANUP_INVALID,
    KERNEL_CLEANUP_FULL
};

struct kernel_task_cleanup_queue {
    bool initialized;
    struct kernel_task_handle entries[KERNEL_TASK_CAPACITY];
    size_t count;
    uint64_t completed;
    uint64_t quarantines;
};

/* Initialize unused, zeroed static FIFO. No allocations or release. */
bool kernel_task_cleanup_init(struct kernel_task_cleanup_queue *queue);

/* Verify bounded count and distinct full task identities. Does not mutate. */
bool kernel_task_cleanup_check(const struct kernel_task_cleanup_queue *queue);

/* Enqueue only a registered TERMINATED task once. Reject stale handles,
 * duplicates, quarantined tasks and a full queue without mutation. */
enum kernel_cleanup_result kernel_task_cleanup_enqueue(
    struct kernel_task_cleanup_queue *queue,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle);

/* On surviving dispatcher/supervisor stack with IF=0, examine at most one
 * full pass of the pending queue. Rotate blocked handles, retire the first
 * eligible handle through registered owner binding, or report BLOCKED.
 * A failed physical release is recorded as quarantined, removed from work
 * queue and never retried. Caller supplies actual observed CPU RSP.
 * No cleanup is performed if an ordinary task owns the CPU. */
enum kernel_cleanup_result kernel_task_cleanup_service(
    struct kernel_task_cleanup_queue *queue,
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    uintptr_t actual_rsp,
    bool (*release_owner)(size_t));
#endif
