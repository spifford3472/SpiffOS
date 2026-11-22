/* Purpose: D5.4 generation-safe priority policy over M10-A dispatch.
 * Responsibilities: bounded candidate validation, wait aging, dedicated
 * supervisor service and authoritative selected dispatch, no second table.
 * Dependencies: kernel-dispatch-d54.h, priority selector and M10-A.
 * Invariants: no changes to task or ages on validation/selection failure.
 * Concurrency: boot CPU, IF=0 for entire call, never IRQ context.
 * Reference: ADR-m10-d54-priority-dispatch.md, AGENTS.md. */
#include "kernel-dispatch-d54.h"
#include <stdint.h>

/* Full handle identity prevents old age surviving slot generation reuse. */
static bool same_handle(struct kernel_task_handle a,
                        struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* Explicit fixed-size initialization; freestanding build cannot call memset. */
bool d54_init(struct d54_dispatch *scheduler)
{
    if(scheduler==NULL || scheduler->initialized) return false;
    scheduler->initialized=true;
    scheduler->decisions=0;
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i) {
        scheduler->age[i]=0;
        scheduler->generation[i]=(struct kernel_task_handle){0};
    }
    return true;
}

/* Validate each queue entry from authoritative task state and use the
 * registered effective priority, which includes transitive lock donation.
 * Candidate ages belong only to this adapter; no duplicate task states. */
enum kernel_task_result d54_dispatch_next(
    struct d54_dispatch *scheduler,
    const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    struct kernel_task_table *tasks,
    struct kernel_task_handle *out)
{
    if(scheduler==NULL || !scheduler->initialized ||
       out==NULL || !kernel_task_check(tasks) ||
       !d53_check(sync,resources,tasks))
        return KERNEL_TASK_INVALID;
    if(tasks->current_slot!=SIZE_MAX) return KERNEL_TASK_BAD_STATE;
    if(tasks->ready_count==0) return KERNEL_TASK_NO_RUNNABLE;
    struct kernel_priority_candidate candidates[KERNEL_TASK_CAPACITY];
    struct kernel_task_handle ids[KERNEL_TASK_CAPACITY];
    size_t count=0;
    for(size_t i=0;i<tasks->ready_count;++i) {
        size_t slot=tasks->ready_queue[i];
        if(slot>=KERNEL_TASK_CAPACITY ||
           tasks->tasks[slot].state!=KERNEL_TASK_RUNNABLE)
            return KERNEL_TASK_CORRUPT;
        ids[count]=(struct kernel_task_handle){
            (uint32_t)slot,tasks->tasks[slot].generation};
        enum kernel_priority_class priority=KERNEL_PRIORITY_TIME_SENSITIVE;
        if(slot!=KERNEL_TASK_SUPERVISOR_SLOT &&
           d53_effective(sync,resources,tasks,ids[count],&priority)!=D53_OK)
            return KERNEL_TASK_CORRUPT;
        candidates[count]=(struct kernel_priority_candidate){
            .slot=(uint32_t)slot,
            .priority=priority,
            .waiting_dispatches=same_handle(scheduler->generation[slot],
                ids[count])?scheduler->age[slot]:0,
            .supervisor=slot==KERNEL_TASK_SUPERVISOR_SLOT
        };
        ++count;
    }
    uint32_t selected_slot=UINT32_MAX;
    if(kernel_priority_choose(candidates,count,scheduler->decisions,
        &selected_slot)!=KERNEL_PRIORITY_OK)
        return KERNEL_TASK_CORRUPT;
    size_t chosen=count;
    for(size_t i=0;i<count;++i)
        if(ids[i].slot==selected_slot) chosen=i;
    if(chosen==count) return KERNEL_TASK_CORRUPT;
    /* The selected dispatch is the ONLY mutator of M10-A task state.
     * No age bookkeeping occurs if the authoritative transition rejects. */
    enum kernel_task_result status=kernel_task_dispatch_selected(
        tasks,ids[chosen],out);
    if(status!=KERNEL_TASK_OK) return status;
    for(size_t i=0;i<count;++i) {
        size_t slot=ids[i].slot;
        if(i==chosen) {
            scheduler->age[slot]=0;
        } else {
            if(!same_handle(scheduler->generation[slot],ids[i]))
                scheduler->age[slot]=0;
            if(scheduler->age[slot]!=UINT64_MAX)
                ++scheduler->age[slot];
        }
        scheduler->generation[slot]=ids[i];
    }
    if(scheduler->decisions!=UINT64_MAX)
        ++scheduler->decisions;
    return KERNEL_TASK_OK;
}
