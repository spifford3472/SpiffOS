/* Purpose: M10-D5.3 bounded synchronization sidecar.
 * Responsibilities: four ranked mutexes, multiple generation-keyed waiters,
 * priority donation, explicit cancellation and fail-closed orphan detection.
 * Dependencies: authoritative M10-A task table and D5.2b resource ledger.
 * Invariants: one wait per task; ascending lock ranks; all refs registered;
 * never automatically free a terminated owner's lock or switch CPU stacks.
 * Concurrency: single boot CPU, caller holds IF=0; never called in IRQ.
 * References: ADR-m10-d53-bounded-synchronization.md, AGENTS.md. */
#ifndef SPIFFOS_KERNEL_SYNC_D53_H
#define SPIFFOS_KERNEL_SYNC_D53_H
#include "kernel-task-resources.h"
#include "kernel-priority-policy.h"

enum { D53_LOCKS=4, D53_TASKS=KERNEL_TASK_SUPERVISOR_SLOT };
enum d53_result {
    D53_OK, D53_BLOCKED, D53_REJECTED, D53_ORPHANED
};

struct d53_lock {
    bool owned;
    struct kernel_task_handle owner;
    struct kernel_task_handle waiters[D53_TASKS];
    size_t waiting;
};
struct d53_profile {
    bool installed;
    struct kernel_task_handle handle;
    enum kernel_priority_class base;
};
struct d53_sync {
    bool initialized;
    struct d53_lock locks[D53_LOCKS];
    struct d53_profile profiles[D53_TASKS];
};

/* Set all bounded state deterministically; requires unused zeroed storage. */
bool d53_init(struct d53_sync *sync);

/* Attach one resource-bound ordinary task and its base scheduling class.
 * Idle recycled slots may be registered with their new generation. */
enum d53_result d53_enroll(struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle task,enum kernel_priority_class priority);

/* Validate lock graph, reference totals and authoritative task states. */
bool d53_check(const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table);

/* Compute four-band effective donation transitively along bounded lock
 * dependencies; never rewrite original task priority. */
enum d53_result d53_effective(const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle task,enum kernel_priority_class *out);

/* Current RUNNING task takes ranked mutex, or registers WAIT and enters
 * BLOCKED; caller must switch off task's CPU stack after D53_BLOCKED. */
enum d53_result d53_acquire(struct d53_sync *sync,
    struct kernel_task_resources *resources,struct kernel_task_table *table,
    unsigned rank,struct kernel_task_handle current);

/* Exact RUNNING owner transfers mutex to highest effective-priority waiter
 * (FIFO tie), removes its wait reference and wakes it into authoritative FIFO. */
enum d53_result d53_release(struct d53_sync *sync,
    struct kernel_task_resources *resources,struct kernel_task_table *table,
    unsigned rank,struct kernel_task_handle current);

/* Surviving dispatcher/supervisor cancels an exact BLOCKED waiter and wakes
 * it BEFORE it can be terminated; never erases terminated-task refs. */
enum d53_result d53_cancel(struct d53_sync *sync,
    struct kernel_task_resources *resources,struct kernel_task_table *table,
    unsigned rank,struct kernel_task_handle waiter);

/* Read-only orphan check: termination never implies automatic lock release. */
enum d53_result d53_orphan(const struct d53_sync *sync,
    const struct kernel_task_table *table,unsigned rank);
#endif
