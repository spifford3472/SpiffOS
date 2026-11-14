/* Purpose: D5.7 fail-closed guard and read-only IRQ donation policy.
 * Responsibilities: prevent nested critical misrelease and compute D5.3
 * effective priority selections without entering mutex mutation from ISR.
 * Dependencies: m10-d57-safety.h and existing D1 selector.
 * Invariants: no frame writes, no task/lock/ledger mutation or I/O.
 * Concurrency: one CPU, caller supplies actual IRQ entry IF evidence.
 * Reference: ADR-m10-d57-preemption-and-priority.md, AGENTS.md. */
#include "m10-d57-safety.h"
#include <stdint.h>

static bool identical(struct kernel_task_handle a,
                      struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation &&
        a.generation!=0;
}

/* Zeroed caller-owned guard is a single-use object; no reset API exists. */
bool d57_guard_init(struct d57_preempt_guard *guard)
{
    if(guard==NULL || guard->initialized || guard->faulted)
        return false;
    guard->initialized=true;
    guard->depth=0;
    guard->owner=(struct kernel_task_handle){0};
    guard->next_serial=0;
    for(size_t i=0;i<D57_MAX_GUARD_DEPTH;++i) guard->serial[i]=0;
    return true;
}

/* A strictly nested guard belongs to one exact running task at a time.
 * The saved cookie detects out-of-order release without relying on slot. */
bool d57_guard_enter(struct d57_preempt_guard *guard,
    struct kernel_task_handle owner,bool in_irq,bool live_if_clear,
    uint32_t *cookie)
{
    if(guard==NULL || !guard->initialized || guard->faulted ||
       cookie==NULL) return false;
    if(in_irq || !live_if_clear ||
       owner.generation==0 || owner.slot>=KERNEL_TASK_CAPACITY ||
       guard->depth>=D57_MAX_GUARD_DEPTH ||
       (guard->depth!=0 && !identical(guard->owner,owner)) ||
       guard->next_serial==UINT32_MAX) {
        guard->faulted=true;
        return false;
    }
    uint32_t serial=++guard->next_serial;
    guard->serial[guard->depth++]=serial;
    guard->owner=owner;
    *cookie=serial;
    return true;
}

/* Incorrect exit order, missing nesting or changed task generation is a
 * permanent fault: never clear the flag by trying to force-unlock. */
bool d57_guard_exit(struct d57_preempt_guard *guard,
    struct kernel_task_handle owner,bool in_irq,bool live_if_clear,
    uint32_t cookie)
{
    if(guard==NULL || !guard->initialized || guard->faulted)
        return false;
    if(in_irq || !live_if_clear || guard->depth==0 ||
       !identical(guard->owner,owner) ||
       cookie==0 || guard->serial[guard->depth-1]!=cookie) {
        guard->faulted=true;
        return false;
    }
    guard->serial[--guard->depth]=0;
    if(guard->depth==0) guard->owner=(struct kernel_task_handle){0};
    return true;
}

/* Rejected interrupt selection never rewrites critical metadata; its
 * caller must still validate the *actual* saved CPU frame and IF flags. */
bool d57_guard_may_switch(const struct d57_preempt_guard *guard,
    bool irq_exit,bool saved_if_enabled,bool live_if_disabled)
{
    return guard!=NULL && guard->initialized && !guard->faulted &&
        guard->depth==0 && irq_exit && saved_if_enabled &&
        live_if_disabled;
}

/* Distinct decision ages are never authoritative task identities. */
bool d57_policy_init(struct d57_irq_policy *policy)
{
    if(policy==NULL || policy->initialized) return false;
    policy->initialized=true;
    policy->decisions=0;
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i) {
        policy->age[i]=0;
        policy->generation[i]=(struct kernel_task_handle){0};
    }
    return true;
}

/* One read-only candidate scan, including transitive D5.3 donation and
 * supervisor's fourth-decision reservation; no raw task state writes. */
bool d57_select(const struct d57_irq_policy *policy,
    const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *tasks,
    struct kernel_task_handle *out)
{
    if(policy==NULL || !policy->initialized || out==NULL ||
       !kernel_task_check(tasks) ||
       !d53_check(sync,resources,tasks) || tasks->ready_count==0)
        return false;
    struct kernel_priority_candidate options[KERNEL_TASK_CAPACITY];
    struct kernel_task_handle identities[KERNEL_TASK_CAPACITY];
    for(size_t i=0;i<tasks->ready_count;++i) {
        size_t slot=tasks->ready_queue[i];
        if(slot>=KERNEL_TASK_CAPACITY ||
           tasks->tasks[slot].state!=KERNEL_TASK_RUNNABLE)
            return false;
        identities[i]=(struct kernel_task_handle){
            (uint32_t)slot,tasks->tasks[slot].generation};
        enum kernel_priority_class priority=KERNEL_PRIORITY_TIME_SENSITIVE;
        if(slot!=KERNEL_TASK_SUPERVISOR_SLOT &&
           d53_effective(sync,resources,tasks,identities[i],&priority)!=D53_OK)
            return false;
        options[i]=(struct kernel_priority_candidate){
            .slot=(uint32_t)slot,
            .priority=priority,
            .waiting_dispatches=identical(policy->generation[slot],
                identities[i])?policy->age[slot]:0,
            .supervisor=slot==KERNEL_TASK_SUPERVISOR_SLOT
        };
    }
    uint32_t slot=UINT32_MAX;
    if(kernel_priority_choose(options,tasks->ready_count,
           policy->decisions,&slot)!=KERNEL_PRIORITY_OK)
        return false;
    for(size_t i=0;i<tasks->ready_count;++i) {
        if(identities[i].slot==slot) {
            *out=identities[i];
            return true;
        }
    }
    return false;
}

/* Caller records decision only after exact M10-A selected generation
 * entered RUNNING. Invalid policy/handle cannot age a stale descriptor. */
bool d57_note_handoff(struct d57_irq_policy *policy,
    const struct kernel_task_table *tasks,
    struct kernel_task_handle selected)
{
    if(policy==NULL || !policy->initialized ||
       !kernel_task_check(tasks) ||
       selected.slot>=KERNEL_TASK_CAPACITY ||
       tasks->current_slot!=selected.slot ||
       tasks->tasks[selected.slot].state!=KERNEL_TASK_RUNNING ||
       tasks->tasks[selected.slot].generation!=selected.generation)
        return false;
    for(size_t i=0;i<tasks->ready_count;++i) {
        size_t slot=tasks->ready_queue[i];
        struct kernel_task_handle id={
            (uint32_t)slot,tasks->tasks[slot].generation};
        if(!identical(policy->generation[slot],id))
            policy->age[slot]=0;
        if(policy->age[slot]!=UINT64_MAX) ++policy->age[slot];
        policy->generation[slot]=id;
    }
    policy->age[selected.slot]=0;
    policy->generation[selected.slot]=selected;
    if(policy->decisions!=UINT64_MAX) ++policy->decisions;
    return true;
}
