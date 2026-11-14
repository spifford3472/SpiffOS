/* Purpose: M10-D5.2b generation-keyed task ownership interlock.
 * Responsibilities: bind exact stack owner, count outstanding references,
 * reject unsafe retirement and quarantine unrecoverable release failures.
 * Dependencies: kernel-task-resources.h, kernel-task-reaper.h.
 * Invariants: bounded static slots; no automatic release of locks or waits;
 * only the surviving dispatcher/supervisor can retire a task.
 * Concurrency: single CPU, caller maintains IF=0 for every mutation; not IRQ.
 * Reference: docs/decisions/ADR-m10-d5-resource-ownership.md. */
#include "kernel-task-resources.h"
#include <limits.h>
#include <stdint.h>

/* Match a whole handle, including generation. Slot comparisons alone would
 * permit a stale owner record to authorize reclaiming a newly reused slot. */
static bool same_handle(struct kernel_task_handle a,
                        struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* Prove metadata and physical resource identities belong to one live
 * generation. Never dereference a stack's contents during this scan. */
bool kernel_task_resources_check(const struct kernel_task_resources *resources,
    const struct kernel_task_table *table)
{
    if (resources==NULL || !resources->initialized ||
        !kernel_task_check(table)) return false;
    for (size_t i=0;i<KERNEL_TASK_CAPACITY;++i) {
        const struct kernel_task_resource_record *r=&resources->task[i];
        if (!r->installed) {
            if (r->quarantined || r->owned_locks || r->registered_waiters)
                return false;
            continue;
        }
        if (r->handle.slot!=i || r->quarantined && r->owned_locks != 0 ||
            kernel_task_get(table,r->handle)==NULL ||
            kernel_task_get(table,r->handle)->reclaimed ||
            !kernel_task_stack_safe_to_release(&r->stack,UINTPTR_MAX))
            return false;
        uintptr_t end=r->stack.bottom+r->stack.bytes;
        for(size_t j=0;j<i;++j) {
            const struct kernel_task_resource_record *other=&resources->task[j];
            if(!other->installed) continue;
            uintptr_t other_end=other->stack.bottom+other->stack.bytes;
            if(r->stack.owner_index==other->stack.owner_index ||
               (r->stack.bottom<other_end && other->stack.bottom<end))
                return false;
        }
    }
    return true;
}

/* Reset caller-owned registry; no implicit teardown of previous bindings. */
bool kernel_task_resources_init(struct kernel_task_resources *resources)
{
    if(resources==NULL || resources->initialized) return false;
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i)
        resources->task[i]=(struct kernel_task_resource_record){0};
    resources->initialized=true;
    return true;
}

/* Validate handle and registry before any mutation. Return NULL for stale
 * identities or unregistered descriptors. */
static struct kernel_task_resource_record *lookup(
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle)
{
    if(!kernel_task_resources_check(resources,table) ||
       handle.slot>=KERNEL_TASK_CAPACITY) return NULL;
    struct kernel_task_resource_record *r=&resources->task[handle.slot];
    if(!r->installed || !same_handle(r->handle,handle)) return NULL;
    return r;
}

/* Publish backing only for a new live descriptor and disjoint owner token.
 * The registry records the same identity used by the task table. */
enum kernel_task_resource_result kernel_task_resources_bind(
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle,
    const struct kernel_task_stack_binding *binding)
{
    if(resources==NULL || binding==NULL || !kernel_task_resources_check(resources,table))
        return KERNEL_RESOURCE_INVALID;
    const struct kernel_task_record *task=kernel_task_get(table,handle);
    if(task==NULL) return KERNEL_RESOURCE_BAD_HANDLE;
    if(handle.slot>=KERNEL_TASK_CAPACITY || resources->task[handle.slot].installed ||
       task->state!=KERNEL_TASK_CREATED)
        return KERNEL_RESOURCE_BAD_STATE;
    if(!kernel_task_stack_safe_to_release(binding,UINTPTR_MAX))
        return KERNEL_RESOURCE_INVALID;
    uintptr_t end=binding->bottom+binding->bytes;
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i) {
        const struct kernel_task_resource_record *existing=&resources->task[i];
        if(!existing->installed) continue;
        uintptr_t existing_end=existing->stack.bottom+existing->stack.bytes;
        if(existing->stack.owner_index==binding->owner_index ||
           (binding->bottom<existing_end && existing->stack.bottom<end))
            return KERNEL_RESOURCE_BAD_STATE;
    }
    resources->task[handle.slot]=(struct kernel_task_resource_record){
        .installed=true,.handle=handle,.stack=*binding};
    return KERNEL_RESOURCE_OK;
}

/* Dispatcher saves a valid suspended RSP; the physical owner stays fixed.
 * Allow terminated task's last saved context after switching to boot. */
enum kernel_task_resource_result kernel_task_resources_saved_rsp(
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle, uintptr_t saved_rsp)
{
    struct kernel_task_resource_record *r=lookup(resources,table,handle);
    if(r==NULL) return KERNEL_RESOURCE_BAD_HANDLE;
    if(r->quarantined) return KERNEL_RESOURCE_QUARANTINED;
    const struct kernel_task_record *task=kernel_task_get(table,handle);
    if(task->state==KERNEL_TASK_RUNNING) return KERNEL_RESOURCE_BAD_STATE;
    struct kernel_task_stack_binding probe=r->stack;
    probe.saved_rsp=saved_rsp;
    if(!kernel_task_stack_safe_to_release(&probe,UINTPTR_MAX))
        return KERNEL_RESOURCE_INVALID;
    r->stack.saved_rsp=saved_rsp;
    return KERNEL_RESOURCE_OK;
}

/* Bound a separate lock/wait manager's reference count; no underflow or
 * wraparound is permitted even after hostile or corrupted call ordering. */
enum kernel_task_resource_result kernel_task_resources_ref(
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle handle,
    enum kernel_task_reference_kind kind, bool acquire)
{
    struct kernel_task_resource_record *r=lookup(resources,table,handle);
    if(r==NULL) return KERNEL_RESOURCE_BAD_HANDLE;
    if(r->quarantined) return KERNEL_RESOURCE_QUARANTINED;
    const struct kernel_task_record *task=kernel_task_get(table,handle);
    if(task->state==KERNEL_TASK_TERMINATED) return KERNEL_RESOURCE_BAD_STATE;
    unsigned *count=kind==KERNEL_RESOURCE_LOCK?&r->owned_locks:
        kind==KERNEL_RESOURCE_WAITER?&r->registered_waiters:NULL;
    if(count==NULL) return KERNEL_RESOURCE_INVALID;
    if(acquire && *count==UINT_MAX) return KERNEL_RESOURCE_OVERFLOW;
    if(!acquire && *count==0) return KERNEL_RESOURCE_BAD_STATE;
    if(acquire) ++*count;
    else --*count;
    return KERNEL_RESOURCE_OK;
}

/* The trusted dispatcher consumes only its registered immutable owner
 * binding; it never trusts a caller-supplied stack address on final release.
 * A failed release quarantines permanently to prevent accidental double
 * free when allocator backing was partly retired. */
enum kernel_task_resource_result kernel_task_resources_retire(
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    struct kernel_task_handle handle, uintptr_t actual_rsp,
    bool (*release_owner)(size_t owner_index))
{
    if(release_owner==NULL) return KERNEL_RESOURCE_INVALID;
    struct kernel_task_resource_record *r=lookup(resources,table,handle);
    if(r==NULL) return KERNEL_RESOURCE_BAD_HANDLE;
    if(r->quarantined) return KERNEL_RESOURCE_QUARANTINED;
    const struct kernel_task_record *task=kernel_task_get(table,handle);
    if(task->state!=KERNEL_TASK_TERMINATED)
        return KERNEL_RESOURCE_BAD_STATE;
    if(r->owned_locks || r->registered_waiters)
        return KERNEL_RESOURCE_BLOCKED;
    if(table->current_slot!=SIZE_MAX &&
       table->current_slot!=KERNEL_TASK_SUPERVISOR_SLOT)
        return KERNEL_RESOURCE_BAD_STATE;
    if(!kernel_task_stack_safe_to_release(&r->stack,actual_rsp))
        return KERNEL_RESOURCE_BAD_STATE;
    enum kernel_task_result result=kernel_task_reap_owned(
        table,handle,&r->stack,actual_rsp,release_owner);
    if(result!=KERNEL_TASK_OK) {
        /* Release failure may be partial: refuse automatic retry forever.
         * Preserve task handle and binding for supervisor diagnosis. */
        r->quarantined=true;
        return KERNEL_RESOURCE_QUARANTINED;
    }
    *r=(struct kernel_task_resource_record){0};
    return KERNEL_RESOURCE_OK;
}
