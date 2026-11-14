/* Purpose: M10-A bounded deterministic cooperative scheduling policy.
 * Responsibilities: validate opaque task handles, manage legal transitions,
 * enforce FIFO run queue invariants and preserve supervisor descriptor slot.
 * Dependencies: kernel-task.h only; no heap, timer, interrupt or OS calls.
 * Invariants: ready queue iff RUNNABLE; exactly one RUNNING or none; no
 * duplicate queue slots; generations never silently wrap on reuse.
 * Concurrency: single CPU, all operations under caller-owned exclusion,
 * never in interrupt context.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "kernel-task.h"

#include <stdint.h>

/* Match a handle to a created, retained record. Slots are range-checked
 * before indexing. The generation prevents stale-ID reuse. */
static bool valid_handle(const struct kernel_task_table *table,
                         struct kernel_task_handle handle)
{
    return handle.slot < KERNEL_TASK_CAPACITY &&
        table->tasks[handle.slot].state != KERNEL_TASK_FREE &&
        table->tasks[handle.slot].generation == handle.generation &&
        handle.generation != 0;
}

/* Append exactly once. Capacity follows the bounded task table and requires
 * a state transition whose caller has already proven membership. */
static void append_ready(struct kernel_task_table *table, size_t slot)
{
    table->ready_queue[table->ready_count++] = slot;
}

/* Remove one known RUNNABLE record, compacting FIFO in stable order.
 * No memory allocation, and no queue wrap arithmetic. */
static void remove_ready(struct kernel_task_table *table, size_t slot)
{
    for (size_t i = 0; i < table->ready_count; ++i) {
        if (table->ready_queue[i] == slot) {
            for (size_t j = i + 1; j < table->ready_count; ++j)
                table->ready_queue[j - 1] = table->ready_queue[j];
            --table->ready_count;
            return;
        }
    }
}

/* Reset all records and ready slots to deterministic unused values.
 * Caller must supply a currently unused descriptor, not live task state. */
bool kernel_task_init(struct kernel_task_table *table)
{
    if (table == NULL || table->initialized) return false;
    for (size_t i = 0; i < KERNEL_TASK_CAPACITY; ++i) {
        table->tasks[i] = (struct kernel_task_record){0};
        table->ready_queue[i] = SIZE_MAX;
    }
    table->ready_count = 0;
    table->current_slot = SIZE_MAX;
    table->initialized = true;
    return true;
}

/* Recompute queue membership and current task cardinality from metadata.
 * Does not trust queue indices before bounds checks. No mutation. */
bool kernel_task_check(const struct kernel_task_table *table)
{
    if (table == NULL || !table->initialized ||
        table->ready_count > KERNEL_TASK_CAPACITY ||
        (table->current_slot != SIZE_MAX &&
         table->current_slot >= KERNEL_TASK_CAPACITY)) return false;
    bool queued[KERNEL_TASK_CAPACITY] = {false};
    for (size_t i = 0; i < table->ready_count; ++i) {
        size_t slot = table->ready_queue[i];
        if (slot >= KERNEL_TASK_CAPACITY || queued[slot] ||
            table->tasks[slot].state != KERNEL_TASK_RUNNABLE) return false;
        queued[slot] = true;
    }
    size_t running = 0;
    for (size_t i = 0; i < KERNEL_TASK_CAPACITY; ++i) {
        const struct kernel_task_record *record = &table->tasks[i];
        if (record->state > KERNEL_TASK_TERMINATED ||
            (record->state == KERNEL_TASK_FREE && record->generation != 0) ||
            (record->state != KERNEL_TASK_FREE && record->generation == 0) ||
            (record->state == KERNEL_TASK_RUNNABLE) != queued[i] ||
            (record->reclaimed && record->state != KERNEL_TASK_TERMINATED) ||
            (record->supervisor && i != KERNEL_TASK_SUPERVISOR_SLOT) ||
            (i == KERNEL_TASK_SUPERVISOR_SLOT &&
             record->state != KERNEL_TASK_FREE && !record->supervisor))
            return false;
        if (record->state == KERNEL_TASK_RUNNING) {
            ++running;
            if (table->current_slot != i) return false;
        }
    }
    return running <= 1 &&
        ((running == 0 && table->current_slot == SIZE_MAX) ||
         (running == 1 && table->current_slot != SIZE_MAX));
}

/* Validate global invariants at every public mutation boundary. A malformed
 * table cannot be operated on speculatively. */
static enum kernel_task_result preflight(struct kernel_task_table *table)
{
    if (table == NULL || !table->initialized) return KERNEL_TASK_INVALID;
    return kernel_task_check(table) ? KERNEL_TASK_OK : KERNEL_TASK_CORRUPT;
}

/* Select the first available slot in its reserved class; terminated records
 * are reusable only when their generations can safely advance. */
enum kernel_task_result kernel_task_create(struct kernel_task_table *table,
    bool supervisor, struct kernel_task_handle *out)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (out == NULL) return KERNEL_TASK_INVALID;
    size_t first = supervisor ? KERNEL_TASK_SUPERVISOR_SLOT : 0;
    size_t last = supervisor ? KERNEL_TASK_CAPACITY :
        KERNEL_TASK_SUPERVISOR_SLOT;
    bool exhausted_generation = false;
    for (size_t i = first; i < last; ++i) {
        struct kernel_task_record *record = &table->tasks[i];
        if (record->state != KERNEL_TASK_FREE &&
            !(record->state == KERNEL_TASK_TERMINATED && record->reclaimed)) continue;
        if (record->generation == UINT32_MAX) {
            exhausted_generation = true;
            continue;
        }
        uint32_t generation = record->generation + 1;
        *record = (struct kernel_task_record){
            .state = KERNEL_TASK_CREATED,
            .generation = generation,
            .supervisor = supervisor
        };
        *out = (struct kernel_task_handle){(uint32_t)i, generation};
        return KERNEL_TASK_OK;
    }
    return exhausted_generation ? KERNEL_TASK_GENERATION_EXHAUSTED :
        KERNEL_TASK_NO_CAPACITY;
}

/* Reject duplicate enqueue rather than making the same task runnable twice. */
enum kernel_task_result kernel_task_make_runnable(
    struct kernel_task_table *table, struct kernel_task_handle handle)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (!valid_handle(table,handle)) return KERNEL_TASK_BAD_HANDLE;
    struct kernel_task_record *record = &table->tasks[handle.slot];
    if (record->state != KERNEL_TASK_CREATED) return KERNEL_TASK_BAD_STATE;
    record->state = KERNEL_TASK_RUNNABLE;
    append_ready(table,handle.slot);
    return KERNEL_TASK_OK;
}

/* Never switch away from a running owner implicitly; dispatch only when
 * current_slot is empty and take the FIFO head. */
enum kernel_task_result kernel_task_dispatch(struct kernel_task_table *table,
    struct kernel_task_handle *out)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (out == NULL) return KERNEL_TASK_INVALID;
    if (table->current_slot != SIZE_MAX) return KERNEL_TASK_BAD_STATE;
    if (table->ready_count == 0) return KERNEL_TASK_NO_RUNNABLE;
    size_t slot = table->ready_queue[0];
    remove_ready(table,slot);
    struct kernel_task_record *record = &table->tasks[slot];
    record->state = KERNEL_TASK_RUNNING;
    if (record->dispatches != UINT64_MAX) ++record->dispatches;
    table->current_slot = slot;
    *out = (struct kernel_task_handle){(uint32_t)slot,record->generation};
    return KERNEL_TASK_OK;
}

/* The caller remains responsible for switching stacks in future M10-B;
 * M10-A only changes which task is eligible to be dispatched next. */
enum kernel_task_result kernel_task_yield(struct kernel_task_table *table,
    struct kernel_task_handle current)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (!valid_handle(table,current)) return KERNEL_TASK_BAD_HANDLE;
    if (table->current_slot != current.slot ||
        table->tasks[current.slot].state != KERNEL_TASK_RUNNING)
        return KERNEL_TASK_BAD_STATE;
    table->tasks[current.slot].state = KERNEL_TASK_RUNNABLE;
    table->current_slot = SIZE_MAX;
    append_ready(table,current.slot);
    return KERNEL_TASK_OK;
}

/* A blocked task is not ready and cannot be dispatched until woken. */
enum kernel_task_result kernel_task_block(struct kernel_task_table *table,
    struct kernel_task_handle current)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (!valid_handle(table,current)) return KERNEL_TASK_BAD_HANDLE;
    if (table->current_slot != current.slot ||
        table->tasks[current.slot].state != KERNEL_TASK_RUNNING)
        return KERNEL_TASK_BAD_STATE;
    table->tasks[current.slot].state = KERNEL_TASK_BLOCKED;
    table->current_slot = SIZE_MAX;
    return KERNEL_TASK_OK;
}

/* Wake appends only blocked tasks and never preempts the running task. */
enum kernel_task_result kernel_task_wake(struct kernel_task_table *table,
    struct kernel_task_handle handle)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (!valid_handle(table,handle)) return KERNEL_TASK_BAD_HANDLE;
    if (table->tasks[handle.slot].state != KERNEL_TASK_BLOCKED)
        return KERNEL_TASK_BAD_STATE;
    table->tasks[handle.slot].state = KERNEL_TASK_RUNNABLE;
    append_ready(table,handle.slot);
    return KERNEL_TASK_OK;
}

/* Reuse is safe at policy level only. Actual stack reclamation is delegated
 * to M10-B after the switcher has returned to another live stack. */
enum kernel_task_result kernel_task_terminate(struct kernel_task_table *table,
    struct kernel_task_handle handle)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (!valid_handle(table,handle)) return KERNEL_TASK_BAD_HANDLE;
    struct kernel_task_record *record = &table->tasks[handle.slot];
    if (record->state == KERNEL_TASK_TERMINATED) return KERNEL_TASK_BAD_STATE;
    if (record->state == KERNEL_TASK_RUNNABLE)
        remove_ready(table,handle.slot);
    if (record->state == KERNEL_TASK_RUNNING)
        table->current_slot = SIZE_MAX;
    record->state = KERNEL_TASK_TERMINATED;
    record->reclaimed = false;
    return KERNEL_TASK_OK;
}

/* The descriptor remains reserved after logical exit. The verified owner
 * must have completed detached-stack and resource cleanup BEFORE this call.
 * No IRQ-context caller, allocation, free or context switching is permitted.
 * Until an actual production reaper is integrated, this is a caller contract
 * and deliberately does not claim to prove stack detachment by itself. */
enum kernel_task_result kernel_task_reap(struct kernel_task_table *table,
    struct kernel_task_handle handle)
{
    enum kernel_task_result status = preflight(table);
    if (status != KERNEL_TASK_OK) return status;
    if (!valid_handle(table, handle)) return KERNEL_TASK_BAD_HANDLE;
    struct kernel_task_record *record = &table->tasks[handle.slot];
    if (record->state != KERNEL_TASK_TERMINATED || record->reclaimed)
        return KERNEL_TASK_BAD_STATE;
    record->reclaimed = true;
    return KERNEL_TASK_OK;
}

/* Borrow immutable metadata. Returning NULL for a stale handle avoids
 * leaking a reused task descriptor through an old identity. */
const struct kernel_task_record *kernel_task_get(
    const struct kernel_task_table *table, struct kernel_task_handle handle)
{
    if (!kernel_task_check(table) || !valid_handle(table,handle)) return NULL;
    return &table->tasks[handle.slot];
}
