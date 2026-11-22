/* Purpose: M10-D5.2b bounded per-task resource ownership gate.
 * Responsibilities: generation-keyed stack bindings, lock/wait reference
 * interlocks, quarantine and supervisor-controlled off-stack retirement.
 * Dependencies: M10-A task table and D5.2a stack reaper only.
 * Invariants: exactly one binding per live task generation; no overlap;
 * unsafe or failed release never makes a descriptor reusable.
 * Concurrency: single boot CPU; caller owns IF=0 for every mutation and
 * must never call from an interrupt handler.
 * References: ADR-m10-d5-resource-ownership.md and AGENTS.md. */
#ifndef SPIFFOS_KERNEL_TASK_RESOURCES_H
#define SPIFFOS_KERNEL_TASK_RESOURCES_H
#include "kernel-task-reaper.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum kernel_task_resource_result {
    KERNEL_RESOURCE_OK,
    KERNEL_RESOURCE_INVALID,
    KERNEL_RESOURCE_BAD_HANDLE,
    KERNEL_RESOURCE_BAD_STATE,
    KERNEL_RESOURCE_OVERFLOW,
    KERNEL_RESOURCE_BLOCKED,
    KERNEL_RESOURCE_QUARANTINED
};

enum kernel_task_reference_kind {
    KERNEL_RESOURCE_LOCK,
    KERNEL_RESOURCE_WAITER
};

/* Owner-index is the memory service's token, NOT necessarily a task slot.
 * The only mutable context field after registration is saved_rsp. */
struct kernel_task_resource_record {
    bool installed;
    bool quarantined;
    struct kernel_task_handle handle;
    struct kernel_task_stack_binding stack;
    unsigned owned_locks;
    unsigned registered_waiters;
};

struct kernel_task_resources {
    bool initialized;
    struct kernel_task_resource_record task[KERNEL_TASK_CAPACITY];
};

/* Initialize caller-zeroed bounded registry once. No allocations or I/O. */
bool kernel_task_resources_init(struct kernel_task_resources *resources);

/* Read-only proof that registry bindings refer to exact live generations.
 * All installed bindings must have nonoverlapping backing and owner tokens. */
bool kernel_task_resources_check(const struct kernel_task_resources *resources,
    const struct kernel_task_table *table);

/* Register one trusted, independent stack owner before dispatch. Reject
 * uncreated/terminated tasks, duplicate ownership and overlapping ranges.
 * The exact saved_rsp is validated before publication. */
enum kernel_task_resource_result kernel_task_resources_bind(
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle,
    const struct kernel_task_stack_binding *binding);

/* Dispatcher-only saved-context refresh after switching away from this
 * task; it cannot alter backing address/size or memory-owner identity. */
enum kernel_task_resource_result kernel_task_resources_saved_rsp(
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle, uintptr_t saved_rsp);

/* Trusted lock/wait managers adjust their own reference count before
 * termination. Counter underflow/overflow and stale handles are rejected
 * without mutation. An interlock, not an implementation of mutexes. */
enum kernel_task_resource_result kernel_task_resources_ref(
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle,
    enum kernel_task_reference_kind kind, bool acquire);

/* Dispatcher/supervisor-only final release with actual observed RSP and
 * IF=0 throughout. The owner callback must be trustworthy and fail closed.
 * Any callback refusal permanently quarantines this binding; no retry.
 * Requires TERMINATED with no locks or waiters; after success removes the
 * binding and reaps the authoritative task descriptor. */
enum kernel_task_resource_result kernel_task_resources_retire(
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    struct kernel_task_handle handle, uintptr_t actual_rsp,
    bool (*release_owner)(size_t owner_index));
#endif
