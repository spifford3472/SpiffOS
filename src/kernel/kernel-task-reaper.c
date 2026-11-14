/* Purpose: M10-D5.2a bounded dispatcher-stack reclamation adapter.
 * Responsibilities: reject active/unowned/invalid contexts before release.
 * Dependencies: kernel-task-reaper.h and kernel-task.c metadata.
 * Invariants: only a verified TERMINATED non-reclaimed handle may release;
 * no failure may publish a reusable slot with owned stack backing.
 * Concurrency: single boot CPU, caller masks IRQ before entry and keeps
 * masking through memory-owner release and descriptor transition.
 * References: ADR-m10-d5-execution-aware-reaper.md and AGENTS.md. */
#include "kernel-task-reaper.h"

#include <stdint.h>

/* Validate arithmetic and the saved outgoing context without dereference.
 * The stack is aligned to the currently proved 16KiB kernel-task model.
 * The two-pointer test catches attempted self-free on an executing stack. */
bool kernel_task_stack_safe_to_release(
    const struct kernel_task_stack_binding *stack, uintptr_t actual_rsp)
{
    if (stack == NULL || stack->bottom == 0 ||
        stack->bytes < 16384u || (stack->bytes & 15u) != 0 ||
        (stack->bottom & 15u) != 0 ||
        stack->bottom > UINTPTR_MAX - stack->bytes)
        return false;
    uintptr_t end = stack->bottom + stack->bytes;
    if (stack->saved_rsp < stack->bottom || stack->saved_rsp >= end ||
        (stack->saved_rsp & 7u) != 0)
        return false;
    /* Execution is still on this backing if RSP falls within its bounds. */
    if (actual_rsp >= stack->bottom && actual_rsp < end)
        return false;
    return actual_rsp != 0;
}

/* The surviving supervisor validates descriptor ownership before invoking
 * the memory owner's atomic release. After that, only D5.1 metadata is
 * reclaimed. A failed release leaves the descriptor inaccessible for reuse.
 * The caller must independently prove no locks/waiters/frames reference it. */
enum kernel_task_result kernel_task_reap_owned(
    struct kernel_task_table *table,
    struct kernel_task_handle handle,
    const struct kernel_task_stack_binding *stack,
    uintptr_t actual_rsp,
    bool (*release_owner)(size_t owner_index))
{
    if (table == NULL || stack == NULL || release_owner == NULL)
        return KERNEL_TASK_INVALID;
    if (!kernel_task_check(table))
        return KERNEL_TASK_CORRUPT;
    const struct kernel_task_record *record = kernel_task_get(table, handle);
    if (record == NULL)
        return KERNEL_TASK_BAD_HANDLE;
    /* A separate ordinary RUNNING task is not a privileged reaper. The
     * permanent external dispatcher or reserved supervisor owns teardown. */
    if (table->current_slot != SIZE_MAX &&
        table->current_slot != KERNEL_TASK_SUPERVISOR_SLOT)
        return KERNEL_TASK_BAD_STATE;
    if (record->state != KERNEL_TASK_TERMINATED || record->reclaimed ||
        table->current_slot == (size_t)handle.slot ||
        !kernel_task_stack_safe_to_release(stack, actual_rsp))
        return KERNEL_TASK_BAD_STATE;
    if (!release_owner(stack->owner_index))
        return KERNEL_TASK_BAD_STATE;
    /* In the trusted single-CPU IF=0 critical section, no task may mutate
     * the handle between validation, successful release and final reap. */
    return kernel_task_reap(table, handle);
}
