/* Purpose: D5.5c exact-generation frame ownership and IRQ tick monitor.
 * Responsibilities: prove a hardware IRQ frame is wholly within a single
 * registered RUNNING task's stack; reject bogus task identity and unsafe
 * critical state; detect missed supervisor service on real tick samples.
 * Dependencies: M10-A, D5.2b registry and M10-D2 fixed 20-qword frame.
 * Invariants: no kernel state mutation or frame writes, no IRQ EOI, scheduler,
 * allocation, logging or physical memory ownership changes.
 * Concurrency: one CPU, caller serializes active-table reads under IF=0.
 * Reference: ADR-m10-d55c-task-frame-budget.md and AGENTS.md. */
#include "m10-d55c-frame-owner.h"
#include "m10-d2-irq-frame.h"
#include <stdint.h>

/* Bounds in subtraction form avoid pointer-end wrap or out-of-range reads.
 * An empty span, pointer below bottom or overflowing end fails closed. */
static bool span_fits(uintptr_t bottom,size_t capacity,
    uintptr_t start,size_t length)
{
    if(capacity==0 || length==0 || bottom>UINTPTR_MAX-capacity ||
       start<bottom) return false;
    uintptr_t offset=start-bottom;
    return offset<=capacity && length<=capacity-offset;
}

/* This isolated guest uses four-level paging; a 48-bit virtual address
 * is canonical iff bits 63:48 sign-extend bit 47. LA57 is not covered. */
static bool canonical(uint64_t value)
{
    return (value>>48)==(((value>>47)&1u)!=0 ?
        UINT64_C(0xffff):UINT64_C(0));
}

/* Authenticate *consistency* with the sole M10-A state owner, not the
 * provenance of the caller-supplied evidence or actual CPU RSP. */
bool d55c_frame_matches(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d55c_irq_snapshot *e)
{
    if(e==NULL || tasks==NULL || resources==NULL ||
       !kernel_task_resources_check(resources,tasks) ||
       e->saved_frame==NULL || e->current.slot>=KERNEL_TASK_SUPERVISOR_SLOT ||
       e->irq_depth!=1 || e->critical_depth!=0 || !e->cpu_if_clear ||
       tasks->current_slot!=e->current.slot) return false;
    const struct kernel_task_record *task=kernel_task_get(tasks,e->current);
    if(task==NULL || task->state!=KERNEL_TASK_RUNNING) return false;
    const struct kernel_task_resource_record *owner=
        &resources->task[e->current.slot];
    if(!owner->installed || owner->quarantined ||
       owner->handle.slot!=e->current.slot ||
       owner->handle.generation!=e->current.generation ||
       owner->owned_locks!=0 || owner->registered_waiters!=0)
        return false;
    uintptr_t pointer=(uintptr_t)e->saved_frame;
    if((pointer&7u)!=0 ||
       !span_fits(owner->stack.bottom,owner->stack.bytes,pointer,
            M10_D2_FRAME_QWORDS*sizeof(uint64_t)) ||
       !span_fits(owner->stack.bottom,owner->stack.bytes,
            owner->stack.saved_rsp,1))
        return false;
    /* Frame pointer is now proven wholly inside registered owner bounds,
     * so individual hardware frame words can be inspected. */
    uint64_t rip=e->saved_frame[M10_D2_RIP];
    uint64_t rsp=e->saved_frame[M10_D2_RSP];
    uint64_t rflags=e->saved_frame[M10_D2_RFLAGS];
    if(!canonical(rip) || !canonical(rsp) ||
       (rflags&UINT64_C(0x202))!=UINT64_C(0x202) ||
       e->saved_frame[M10_D2_CS]!=e->expected_cs ||
       e->saved_frame[M10_D2_SS]!=e->expected_ss ||
       !span_fits(owner->stack.bottom,owner->stack.bytes,(uintptr_t)rsp,1))
        return false;
    /* The CPU-saved application RSP belongs ABOVE the saved register frame,
     * and is not the current IRQ handler's C-aligned stack pointer. */
    if((uintptr_t)rsp<=pointer+
           M10_D2_FRAME_QWORDS*sizeof(uint64_t)-1u)
        return false;
    return true;
}

/* Fixed monotonic hardware-tick budget, never a scheduling sequence number.
 * Timeout is strict > budget; equal limit remains legal. */
bool d55c_tick_init(struct d55c_tick_service *watch,
    uint64_t initial_tick,uint64_t deadline_ticks)
{
    if(watch==NULL || watch->initialized || deadline_ticks==0)
        return false;
    *watch=(struct d55c_tick_service){
        .initialized=true,
        .last_tick=initial_tick,
        .last_completed_service_tick=initial_tick,
        .deadline_ticks=deadline_ticks
    };
    return true;
}

/* Clock rollback, unavailable tick and failure to complete service all
 * latch the same permanent diagnostic fault. No recovery by later sampling. */
bool d55c_tick_observe(struct d55c_tick_service *watch,
    bool sample_valid,uint64_t hardware_tick)
{
    if(watch==NULL || !watch->initialized || watch->faulted ||
       !sample_valid || hardware_tick<watch->last_tick ||
       hardware_tick<watch->last_completed_service_tick) {
        if(watch!=NULL && watch->initialized) watch->faulted=true;
        return false;
    }
    uint64_t gap=hardware_tick-watch->last_completed_service_tick;
    if(gap>watch->deadline_ticks) {
        watch->faulted=true;
        return false;
    }
    if(gap>watch->max_observed_gap_ticks)
        watch->max_observed_gap_ticks=gap;
    watch->last_tick=hardware_tick;
    return true;
}

/* A dispatch decision is not service completion. The trusted supervisor
 * must have executed and returned on a surviving context to call this. */
bool d55c_tick_service_completed(struct d55c_tick_service *watch,
    bool verified_supervisor_context,bool sample_valid,uint64_t hardware_tick)
{
    if(!verified_supervisor_context ||
       !d55c_tick_observe(watch,sample_valid,hardware_tick))
        return false;
    watch->last_completed_service_tick=hardware_tick;
    if(watch->service_completions!=UINT64_MAX)
        ++watch->service_completions;
    return true;
}
