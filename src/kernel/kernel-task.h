/* Purpose: bounded, hosted-testable M10-A kernel task state and scheduler API.
 * Responsibilities: opaque generation-tagged handles, state transitions, FIFO
 * round robin, a reserved supervisor descriptor, invariant validation.
 * Dependencies: stdbool/stdint, no host OS services, heap or hardware.
 * Invariants: at most one RUNNING task, FIFO contains exactly RUNNABLE tasks,
 * each task gets one unique live handle; ordinary tasks cannot use slot 15.
 * IRQ/concurrency: caller serializes operations with interrupts disabled when
 * embedded in kernel; no operation is safe to call from IRQ context.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#ifndef SPIFFOS_KERNEL_TASK_H
#define SPIFFOS_KERNEL_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    KERNEL_TASK_CAPACITY = 16,
    KERNEL_TASK_SUPERVISOR_SLOT = KERNEL_TASK_CAPACITY - 1
};

enum kernel_task_state {
    KERNEL_TASK_FREE,
    KERNEL_TASK_CREATED,
    KERNEL_TASK_RUNNABLE,
    KERNEL_TASK_RUNNING,
    KERNEL_TASK_BLOCKED,
    KERNEL_TASK_TERMINATED
};

enum kernel_task_result {
    KERNEL_TASK_OK,
    KERNEL_TASK_INVALID,
    KERNEL_TASK_NO_CAPACITY,
    KERNEL_TASK_BAD_HANDLE,
    KERNEL_TASK_BAD_STATE,
    KERNEL_TASK_NO_RUNNABLE,
    KERNEL_TASK_GENERATION_EXHAUSTED,
    KERNEL_TASK_CORRUPT
};

/* A handle is not a virtual address or external process ABI.
 * Generation changes on reuse so stale handles cannot control new tasks. */
struct kernel_task_handle {
    uint32_t slot;
    uint32_t generation;
};

/* Caller-owned record: M10-A owns only lifecycle metadata. M10-B will add
 * execution context and explicit stack allocation ownership separately. */
struct kernel_task_record {
    enum kernel_task_state state;
    uint32_t generation;
    bool supervisor;
    uint64_t dispatches;
    uint64_t total_ticks;
};

/* One CPU owns the entire table. FIFO queue stores slot indices, never
 * pointers; entry_count and current_slot determine queue/running membership.
 * current_slot == SIZE_MAX represents the external bootstrap/idle context. */
struct kernel_task_table {
    struct kernel_task_record tasks[KERNEL_TASK_CAPACITY];
    size_t ready_queue[KERNEL_TASK_CAPACITY];
    size_t ready_count;
    size_t current_slot;
    bool initialized;
};

/* Initialize unused zeroed state. No allocations, IO or concurrency. */
bool kernel_task_init(struct kernel_task_table *table);

/* Verify all state/queue/current invariants without mutation. */
bool kernel_task_check(const struct kernel_task_table *table);

/* Create an ordinary task in slots 0..14; supervisor==true uses only slot 15.
 * Created tasks are NOT runnable until kernel_task_make_runnable. Returns
 * explicit error on exhaustion or generation wrap; output stays unchanged on
 * failure. No stack allocation, context initialization or hardware access. */
enum kernel_task_result kernel_task_create(struct kernel_task_table *table,
    bool supervisor, struct kernel_task_handle *out);

/* CREATED -> RUNNABLE; append once to the FIFO. */
enum kernel_task_result kernel_task_make_runnable(
    struct kernel_task_table *table, struct kernel_task_handle handle);

/* Consume oldest READY and make RUNNING; returns NO_RUNNABLE if queue empty
 * or another task already owns the CPU. Only the dispatcher calls this. */
enum kernel_task_result kernel_task_dispatch(struct kernel_task_table *table,
    struct kernel_task_handle *out);

/* Current RUNNING -> RUNNABLE at FIFO tail; requires exact current handle.
 * This changes policy metadata only: no actual context switch occurs in A. */
enum kernel_task_result kernel_task_yield(struct kernel_task_table *table,
    struct kernel_task_handle current);

/* RUNNING -> BLOCKED; remove active slot. No other runnable slot is changed. */
enum kernel_task_result kernel_task_block(struct kernel_task_table *table,
    struct kernel_task_handle current);

/* BLOCKED -> RUNNABLE; append once to FIFO. */
enum kernel_task_result kernel_task_wake(struct kernel_task_table *table,
    struct kernel_task_handle handle);

/* CREATED/RUNNABLE/RUNNING/BLOCKED -> TERMINATED; removing queue/current
 * references before exposing termination. Never frees a stack or descriptor. */
enum kernel_task_result kernel_task_terminate(struct kernel_task_table *table,
    struct kernel_task_handle handle);

/* Read a valid live/terminated record by handle; caller does not own returned
 * descriptor pointer and may not modify it. */
const struct kernel_task_record *kernel_task_get(
    const struct kernel_task_table *table, struct kernel_task_handle handle);

#endif
