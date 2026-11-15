/* Purpose: D5.8b Phase 1 lock-backed read-only IRQ eligibility checks.
 * Responsibilities: fail closed on stale graph, orphan, waiter/owner
 * conflict while leaving all D5.3 and M10-A state unchanged.
 * Dependencies: m10-d58b-admission.h; no heap, frame reads or x86 calls.
 * Invariants: a successful eligibility is NOT sufficient for IRETQ.
 * Concurrency: single boot CPU; caller serializes snapshots with IF=0.
 * Reference: ADR-m10-d58b-frame-ownership-gate.md, AGENTS.md. */
#include "m10-d58b-admission.h"

/* A blocked waiter is a non-dispatchable identity; all checked RUNNABLE
 * records have zero outstanding waits, but may own correctly linked locks. */
static bool dispatchable_state(enum kernel_task_state expected,
    enum kernel_task_state actual,unsigned waiter_refs)
{
    return (expected==KERNEL_TASK_RUNNING ||
            expected==KERNEL_TASK_RUNNABLE) &&
           actual==expected && waiter_refs==0;
}

/* d53_check confirms all registered reference counts and exact generations,
 * but intentionally permits orphaned terminated owners for diagnosis.
 * IRQ scheduling instead refuses the entire graph until reviewed recovery. */
static bool graph_safe(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,const struct d53_sync *sync)
{
    if(!d53_check(sync,resources,tasks)) return false;
    for(unsigned rank=0;rank<D53_LOCKS;++rank)
        if(d53_orphan(sync,tasks,rank)!=D53_OK) return false;
    return true;
}

/* No frame dereference: true means only that the registered lock graph
 * cannot disqualify this exact RUNNING/RUNNABLE generation from a later
 * hardware-backed frame and critical-section admission gate. */
bool d58b_lock_frame_eligible(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d53_sync *sync,struct kernel_task_handle task,
    enum kernel_task_state required,bool irq_exit,bool live_if_disabled,
    bool saved_if_enabled,bool guard_clear)
{
    if(!irq_exit || !live_if_disabled || !saved_if_enabled || !guard_clear ||
       task.slot>=KERNEL_TASK_SUPERVISOR_SLOT || task.generation==0 ||
       !graph_safe(tasks,resources,sync)) return false;
    const struct kernel_task_record *record=kernel_task_get(tasks,task);
    const struct kernel_task_resource_record *owner=&resources->task[task.slot];
    if(record==NULL || !owner->installed || owner->quarantined ||
       owner->handle.slot!=task.slot ||
       owner->handle.generation!=task.generation ||
       !sync->profiles[task.slot].installed ||
       sync->profiles[task.slot].handle.slot!=task.slot ||
       sync->profiles[task.slot].handle.generation!=task.generation ||
       !dispatchable_state(required,record->state,owner->registered_waiters))
        return false;
    return (required!=KERNEL_TASK_RUNNING ||
            tasks->current_slot==task.slot);
}

/* A BLOCKED task cannot receive IRQ handoff. Its real x86 stack still
 * needs an explicit, independently tested non-IRQ transfer to a survivor. */
bool d58b_blocked_departure_metadata(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d53_sync *sync,struct kernel_task_handle blocked,
    bool in_irq,bool live_if_disabled)
{
    if(in_irq || !live_if_disabled ||
       blocked.slot>=KERNEL_TASK_SUPERVISOR_SLOT ||
       blocked.generation==0 || !graph_safe(tasks,resources,sync) ||
       tasks->current_slot!=SIZE_MAX) return false;
    const struct kernel_task_record *record=kernel_task_get(tasks,blocked);
    const struct kernel_task_resource_record *r=&resources->task[blocked.slot];
    return record!=NULL && record->state==KERNEL_TASK_BLOCKED &&
        r->installed && !r->quarantined &&
        r->handle.slot==blocked.slot &&
        r->handle.generation==blocked.generation &&
        r->registered_waiters!=0;
}
