/* Purpose: M10-D5.8a single-CPU deferred IRQ observation implementation.
 * Responsibilities: bounded fault-latched queue of task/tick witnesses;
 * no scheduling decisions, synchronization mutation or hardware access.
 * Dependencies: m10-d58-notify.h, M10-A read-only metadata validation.
 * Invariants: only a live current task can be noted; no stale observations
 * are consumed; no mutation on failed reads; no implicit fault recovery.
 * Concurrency: IF=0 required; one IRQ producer and supervisor consumer.
 * Reference: ADR-m10-d58-irq-mutex-integration.md, AGENTS.md. */
#include "m10-d58-notify.h"

/* Read-only generation validation, including the exact M10-A RUNNING slot. */
static bool current_matches(const struct kernel_task_table *tasks,
                            struct kernel_task_handle identity)
{
    return tasks!=NULL && kernel_task_check(tasks) &&
        identity.slot<KERNEL_TASK_CAPACITY && identity.generation!=0 &&
        tasks->current_slot==identity.slot &&
        tasks->tasks[identity.slot].generation==identity.generation &&
        tasks->tasks[identity.slot].state==KERNEL_TASK_RUNNING;
}

/* A zeroed object is initialized once, not implicitly repaired after fault. */
bool d58_queue_init(struct d58_notice_queue *queue)
{
    if(queue==NULL || queue->initialized || queue->faulted ||
       queue->head!=0 || queue->count!=0 || queue->latest_tick!=0)
        return false;
    queue->initialized=true;
    return true;
}

/* Source is the actual interrupted RUNNING identity, not a chosen target.
 * The caller must independently validate live IF and IRQ provenance. */
bool d58_note_irq(struct d58_notice_queue *queue,
    const struct kernel_task_table *tasks,
    struct kernel_task_handle observed,uint64_t tick,
    bool irq_exit,bool live_if_disabled)
{
    if(queue==NULL || !queue->initialized || queue->faulted) return false;
    if(!irq_exit || !live_if_disabled ||
       !current_matches(tasks,observed) ||
       queue->count>=D58_NOTICE_CAPACITY ||
       tick<queue->latest_tick) {
        queue->faulted=true;
        return false;
    }
    /* Count is bounded before computing index; no heap or pointer storage. */
    size_t index=(queue->head+queue->count)%D58_NOTICE_CAPACITY;
    queue->records[index]=(struct d58_notice){observed,tick};
    ++queue->count;
    queue->latest_tick=tick;
    return true;
}

/* An observation does not authorize a lock operation. The caller can use
 * it only as diagnostic evidence; revalidate authoritative state again at
 * each independently protected non-IRQ mutation boundary. */
bool d58_take_supervisor(struct d58_notice_queue *queue,
    const struct kernel_task_table *tasks,struct d58_notice *out,
    bool in_irq,bool live_if_disabled,bool supervisor_context)
{
    if(queue==NULL || !queue->initialized || queue->faulted) return false;
    if(in_irq || !live_if_disabled || !supervisor_context || out==NULL ||
       queue->head>=D58_NOTICE_CAPACITY ||
       queue->count>D58_NOTICE_CAPACITY ||
       !kernel_task_check(tasks)) {
        queue->faulted=true;
        return false;
    }
    if(queue->count==0) return false;
    const struct d58_notice item=queue->records[queue->head];
    /* A recorded task may now be READY or BLOCKED after a legitimate
     * handoff. TERMINATED, reused or missing records are stale and refused. */
    if(item.observed.slot>=KERNEL_TASK_CAPACITY ||
       item.observed.generation==0 ||
       tasks->tasks[item.observed.slot].generation!=item.observed.generation ||
       tasks->tasks[item.observed.slot].state==KERNEL_TASK_FREE ||
       tasks->tasks[item.observed.slot].state==KERNEL_TASK_TERMINATED) {
        queue->faulted=true;
        return false;
    }
    *out=item;
    queue->records[queue->head]=(struct d58_notice){0};
    queue->head=(queue->head+1)%D58_NOTICE_CAPACITY;
    --queue->count;
    return true;
}
