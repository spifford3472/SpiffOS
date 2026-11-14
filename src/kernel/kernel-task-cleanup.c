/* Purpose: M10-D5.2c single-CPU bounded cleanup dispatcher.
 * Responsibilities: validate queue identities, retain blocked references,
 * safely delegate eligible off-stack retirements and quarantine failures.
 * Dependencies: kernel-task-cleanup.h, authoritative D5.2b resource ledger.
 * Invariants: only boot/supervisor context releases; no circular allocations;
 * quarantine ends automatic retries without pretending backing was freed.
 * Concurrency: caller owns uninterrupted IF=0; never usable from IRQ.
 * References: ADR-m10-d5-supervisor-cleanup.md and AGENTS.md. */
#include "kernel-task-cleanup.h"
#include <stdint.h>

/* Equality must cover slot AND generation; comparing slots alone would
 * alias an old terminated task with the next task in its slot. */
static bool equal_handles(struct kernel_task_handle a,
                          struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* The queue uses fixed-order compaction rather than a linked allocation
 * scheme because there are at most 16 task slots. */
static void take_front(struct kernel_task_cleanup_queue *queue)
{
    for(size_t i=1;i<queue->count;++i)
        queue->entries[i-1]=queue->entries[i];
    --queue->count;
}

/* Fresh queue initialization is legal only for caller-zeroed storage. */
bool kernel_task_cleanup_init(struct kernel_task_cleanup_queue *queue)
{
    if(queue==NULL || queue->initialized) return false;
    *queue=(struct kernel_task_cleanup_queue){.initialized=true};
    return true;
}

/* Reject malformed identity, impossible size or duplicate full handles. */
bool kernel_task_cleanup_check(const struct kernel_task_cleanup_queue *queue)
{
    if(queue==NULL || !queue->initialized ||
       queue->count>KERNEL_TASK_CAPACITY) return false;
    for(size_t i=0;i<queue->count;++i) {
        if(queue->entries[i].slot>=KERNEL_TASK_CAPACITY ||
           queue->entries[i].generation==0) return false;
        for(size_t j=0;j<i;++j)
            if(equal_handles(queue->entries[j],queue->entries[i]))
                return false;
    }
    return true;
}

/* Queue entries cannot be fabricated for a stopped-but-unregistered task.
 * Presence and generation are proved against both authoritative records. */
enum kernel_cleanup_result kernel_task_cleanup_enqueue(
    struct kernel_task_cleanup_queue *queue,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle)
{
    if(!kernel_task_cleanup_check(queue) ||
       !kernel_task_resources_check(resources,table))
        return KERNEL_CLEANUP_INVALID;
    const struct kernel_task_record *task=kernel_task_get(table,handle);
    if(task==NULL || task->state!=KERNEL_TASK_TERMINATED ||
       task->reclaimed || handle.slot>=KERNEL_TASK_CAPACITY)
        return KERNEL_CLEANUP_INVALID;
    const struct kernel_task_resource_record *owner=&resources->task[handle.slot];
    if(!owner->installed || owner->quarantined ||
       !equal_handles(owner->handle,handle))
        return KERNEL_CLEANUP_INVALID;
    for(size_t i=0;i<queue->count;++i)
        if(equal_handles(queue->entries[i],handle))
            return KERNEL_CLEANUP_INVALID;
    if(queue->count==KERNEL_TASK_CAPACITY)
        return KERNEL_CLEANUP_FULL;
    queue->entries[queue->count++]=handle;
    return KERNEL_CLEANUP_OK;
}

/* This is a bounded supervisor service, NOT an IRQ-handler job or automatic
 * mutex recovery. Blocked entries rotate so a later safe task can progress;
 * the service considers only the count observed at entry. */
enum kernel_cleanup_result kernel_task_cleanup_service(
    struct kernel_task_cleanup_queue *queue,
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    uintptr_t actual_rsp,
    bool (*release_owner)(size_t))
{
    if(!kernel_task_cleanup_check(queue) ||
       !kernel_task_resources_check(resources,table) ||
       release_owner==NULL || actual_rsp==0 ||
       (table->current_slot!=SIZE_MAX &&
        table->current_slot!=KERNEL_TASK_SUPERVISOR_SLOT))
        return KERNEL_CLEANUP_INVALID;
    if(queue->count==0) return KERNEL_CLEANUP_EMPTY;
    size_t scanned=queue->count;
    for(size_t i=0;i<scanned;++i) {
        struct kernel_task_handle handle=queue->entries[0];
        if(handle.slot>=KERNEL_TASK_CAPACITY)
            return KERNEL_CLEANUP_INVALID;
        const struct kernel_task_resource_record *record=
            &resources->task[handle.slot];
        const struct kernel_task_record *task=kernel_task_get(table,handle);
        if(!record->installed || record->quarantined ||
           !equal_handles(record->handle,handle) || task==NULL ||
           task->state!=KERNEL_TASK_TERMINATED || task->reclaimed)
            return KERNEL_CLEANUP_INVALID;
        if(record->owned_locks || record->registered_waiters) {
            /* One rotation preserves the task and makes later tasks eligible
             * within this bounded service visit. No reference is cleared. */
            take_front(queue);
            queue->entries[queue->count++]=handle;
            continue;
        }
        /* No queue mutation before owner validation; an invalid executing
         * RSP cannot cause a physical release or destroy the request. */
        if(!kernel_task_stack_safe_to_release(&record->stack,actual_rsp))
            return KERNEL_CLEANUP_INVALID;
        enum kernel_task_resource_result result=
            kernel_task_resources_retire(resources,table,handle,
                                         actual_rsp,release_owner);
        if(result==KERNEL_RESOURCE_OK) {
            take_front(queue);
            if(queue->completed!=UINT64_MAX) ++queue->completed;
            return KERNEL_CLEANUP_OK;
        }
        if(result==KERNEL_RESOURCE_QUARANTINED) {
            /* Registry preserves poisoned binding; remove only the work
             * entry, and forbid any unverified second physical free. */
            take_front(queue);
            if(queue->quarantines!=UINT64_MAX) ++queue->quarantines;
            return KERNEL_CLEANUP_QUARANTINED;
        }
        return KERNEL_CLEANUP_INVALID;
    }
    return KERNEL_CLEANUP_BLOCKED;
}
