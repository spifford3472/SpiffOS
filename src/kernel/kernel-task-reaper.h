/* Purpose: retire a stopped kernel task's execution stack safely.
 * Responsibilities: validate task identity, stack range, saved context and
 * current-stack separation before an owner-authorized release and D5.1 reap.
 * Dependencies: kernel-task.h, stdint; no heap, hardware or host OS calls.
 * Invariants: release precedes metadata reuse; failure never reaps a live
 * stack; a failed memory-owner release leaves the descriptor unreclaimed.
 * Concurrency: boot CPU only, interrupts disabled by caller throughout;
 * forbidden in interrupt context; release function must be all-or-nothing.
 * References: ADR-m10-d5-production-task-model.md and
 * ADR-m10-d5-execution-aware-reaper.md. */
#ifndef SPIFFOS_KERNEL_TASK_REAPER_H
#define SPIFFOS_KERNEL_TASK_REAPER_H
#include "kernel-task.h"
#include <stddef.h>
#include <stdint.h>

struct kernel_task_stack_binding {
    uintptr_t bottom;     /* Start of exclusive mapped stack backing. */
    size_t bytes;         /* Mapped contiguous stack length. */
    uintptr_t saved_rsp;  /* Saved outgoing context's stack pointer. */
    size_t owner_index;   /* Exact memory-owner token, not a task slot. */
};

/* Prove geometry without touching or freeing caller-owned memory.
 * actual_rsp is the dispatcher's REAL RSP measured in the integration
 * call site, not a number from a task being terminated.
 * Both pointers must be canonical valid mapped kernel addresses as
 * independently verified by the caller; this pure helper checks only
 * alignment, range, overflow and non-overlap. */
bool kernel_task_stack_safe_to_release(
    const struct kernel_task_stack_binding *stack, uintptr_t actual_rsp);

/* Complete a verified stopped task's release while running on a different
 * surviving stack. Owner callback releases only owner_index's exact backing,
 * returns false without partial release on failure, and may not edit the
 * table or change execution context. Requires exclusive IF=0 operation.
 * On a successful owner release, call the D5.1 reaper once.
 * No IRQ handlers or untrusted callers permitted. */
enum kernel_task_result kernel_task_reap_owned(
    struct kernel_task_table *table,
    struct kernel_task_handle handle,
    const struct kernel_task_stack_binding *stack,
    uintptr_t actual_rsp,
    bool (*release_owner)(size_t owner_index));
#endif
