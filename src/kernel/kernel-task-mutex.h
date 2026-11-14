/* Purpose: M10-D5.2d authoritative task blocking/waking mutex proof.
 * Responsibilities: one-owner/one-waiter mutex, exact-generation identity,
 * registered lock/wait references and M10-A scheduler transitions.
 * Dependencies: kernel-task-resources.h and M10-A task lifecycle.
 * Invariants: only an actual RUNNING task can acquire or release; a blocked
 * waiter is not in ready FIFO; safe release transfers ownership before wake.
 * Concurrency: one boot CPU; IF=0 for entire call, no IRQ handler use.
 * Reference: docs/decisions/ADR-m10-d5-mutex-block-wake.md, AGENTS.md. */
#ifndef SPIFFOS_KERNEL_TASK_MUTEX_H
#define SPIFFOS_KERNEL_TASK_MUTEX_H
#include "kernel-task-resources.h"

enum kernel_task_mutex_result {
    KERNEL_MUTEX_OK,
    KERNEL_MUTEX_BLOCKED,
    KERNEL_MUTEX_REJECTED
};

struct kernel_task_mutex {
    bool initialized;
    bool owned;
    bool waiting;
    struct kernel_task_handle owner;
    struct kernel_task_handle waiter;
};

/* Initialize zero-valued caller storage. No memory allocation or I/O. */
bool kernel_task_mutex_init(struct kernel_task_mutex *lock);

/* RUNNING current task acquires unowned mutex, or registers one waiter
 * and transitions RUNNING -> BLOCKED on contention. On BLOCKED return,
 * the caller MUST actually switch to the surviving dispatcher context
 * before another task is dispatched; this function switches no CPU stack.
 * Rejecting a call changes neither task, registry nor mutex. */
enum kernel_task_mutex_result kernel_task_mutex_acquire(
    struct kernel_task_mutex *lock,
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    struct kernel_task_handle current);

/* RUNNING exact owner releases. If waiter is still BLOCKED, register its
 * ownership, remove waiter ref, relinquish old owner's reference, and wake
 * it into M10-A FIFO. Does not preempt or context-switch. Stale or terminal
 * identities, arbitrary callers and reference overflow fail unchanged. */
enum kernel_task_mutex_result kernel_task_mutex_release(
    struct kernel_task_mutex *lock,
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    struct kernel_task_handle current);
#endif
