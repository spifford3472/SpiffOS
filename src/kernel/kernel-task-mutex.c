/* Purpose: D5.2d mutex bound to authoritative task and resource tables.
 * Responsibilities: scheduler BLOCKED/WAKE transitions and exact registered
 * owner/waiter ref transfer under one IF=0 serialized critical section.
 * Dependencies: kernel-task-mutex.h, kernel-task.c and task-resources.c.
 * Invariants: state/ref count mutation follows complete preflight; no
 * automatic dead-owner recovery, IRQ blocking, allocation or kernel ABI.
 * Concurrency: single boot CPU, IF=0, never IRQ context.
 * Reference: ADR-m10-d5-mutex-block-wake.md and AGENTS.md. */
#include "kernel-task-mutex.h"
#include <limits.h>

/* Full generation comparison prevents old slot holders from controlling
 * a task created later in a recycled slot. Pure helper: no mutations. */
static bool same_handle(struct kernel_task_handle a,
                        struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* Require a valid, attached, non-quarantined task and expected state.
 * The caller has already validated the global task and resource tables. */
static bool usable(const struct kernel_task_resources *resources,
                   const struct kernel_task_table *table,
                   struct kernel_task_handle handle,
                   enum kernel_task_state required)
{
    if(handle.slot>=KERNEL_TASK_SUPERVISOR_SLOT) return false;
    const struct kernel_task_record *task=kernel_task_get(table,handle);
    if(task==NULL || task->state!=required) return false;
    const struct kernel_task_resource_record *owner=&resources->task[handle.slot];
    return owner->installed && !owner->quarantined &&
           same_handle(owner->handle,handle);
}

/* All public operations require registered exact identities; caller must
 * serialize with IF=0 because CPU task transitions are not atomic alone. */
static bool preflight(const struct kernel_task_mutex *lock,
                      const struct kernel_task_resources *resources,
                      const struct kernel_task_table *table)
{
    return lock!=NULL && lock->initialized &&
           kernel_task_resources_check(resources,table) &&
           (!lock->waiting || lock->owned) &&
           (!lock->owned || (lock->owner.slot<KERNEL_TASK_SUPERVISOR_SLOT &&
             kernel_task_get(table,lock->owner)!=NULL)) &&
           (!lock->waiting ||
            (lock->waiter.slot<KERNEL_TASK_SUPERVISOR_SLOT &&
             kernel_task_get(table,lock->waiter)!=NULL &&
             !same_handle(lock->owner,lock->waiter)));
}

/* Set only fixed scalar fields to avoid freestanding library memset. */
bool kernel_task_mutex_init(struct kernel_task_mutex *lock)
{
    if(lock==NULL || lock->initialized) return false;
    lock->initialized=true;
    lock->owned=false;
    lock->waiting=false;
    lock->owner.slot=0;
    lock->owner.generation=0;
    lock->waiter.slot=0;
    lock->waiter.generation=0;
    return true;
}

/* Acquiring an unowned mutex records ownership; contention registers one
 * waiter and blocks the actual running task in authoritative M10-A state.
 * A blocked return is a scheduler policy handoff, not a stack switch. */
enum kernel_task_mutex_result kernel_task_mutex_acquire(
    struct kernel_task_mutex *lock,
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    struct kernel_task_handle current)
{
    if(!preflight(lock,resources,table) ||
       !usable(resources,table,current,KERNEL_TASK_RUNNING) ||
       table->current_slot!=current.slot)
        return KERNEL_MUTEX_REJECTED;
    if(!lock->owned) {
        if(lock->waiting ||
           resources->task[current.slot].owned_locks==UINT_MAX)
            return KERNEL_MUTEX_REJECTED;
        if(kernel_task_resources_ref(resources,table,current,
            KERNEL_RESOURCE_LOCK,true)!=KERNEL_RESOURCE_OK)
            return KERNEL_MUTEX_REJECTED;
        lock->owner=current;
        lock->owned=true;
        return KERNEL_MUTEX_OK;
    }
    if(lock->waiting || same_handle(lock->owner,current) ||
       !usable(resources,table,lock->owner,KERNEL_TASK_RUNNABLE) &&
       !usable(resources,table,lock->owner,KERNEL_TASK_RUNNING) ||
       resources->task[current.slot].registered_waiters==UINT_MAX ||
       resources->task[lock->owner.slot].owned_locks==0)
        return KERNEL_MUTEX_REJECTED;
    /* No concurrency is permitted between preflight and transitions.
     * kernel_task_block can now only succeed for this current RUNNING task. */
    if(kernel_task_resources_ref(resources,table,current,
        KERNEL_RESOURCE_WAITER,true)!=KERNEL_RESOURCE_OK)
        return KERNEL_MUTEX_REJECTED;
    if(kernel_task_block(table,current)!=KERNEL_TASK_OK) {
        /* Defensive rollback: failed policy transition cannot retain a
         * waiter reference. With IF=0 and validated state this is unreachable. */
        (void)kernel_task_resources_ref(resources,table,current,
            KERNEL_RESOURCE_WAITER,false);
        return KERNEL_MUTEX_REJECTED;
    }
    lock->waiter=current;
    lock->waiting=true;
    return KERNEL_MUTEX_BLOCKED;
}

/* Unlock never invents wakeups: transfer a registered ownership reference,
 * remove the exact waiter ref, drop prior owner ref, then wake once. */
enum kernel_task_mutex_result kernel_task_mutex_release(
    struct kernel_task_mutex *lock,
    struct kernel_task_resources *resources,
    struct kernel_task_table *table,
    struct kernel_task_handle current)
{
    if(!preflight(lock,resources,table) || !lock->owned ||
       !same_handle(lock->owner,current) ||
       !usable(resources,table,current,KERNEL_TASK_RUNNING) ||
       table->current_slot!=current.slot ||
       resources->task[current.slot].owned_locks==0)
        return KERNEL_MUTEX_REJECTED;
    if(lock->waiting) {
        struct kernel_task_handle waiter=lock->waiter;
        if(!usable(resources,table,waiter,KERNEL_TASK_BLOCKED) ||
           resources->task[waiter.slot].registered_waiters==0 ||
           resources->task[waiter.slot].owned_locks==UINT_MAX ||
           table->ready_count>=KERNEL_TASK_CAPACITY)
            return KERNEL_MUTEX_REJECTED;
        /* One-CPU IF=0 plus exact preflight makes all four operations
         * infallible; no unrelated actor can invalidate a reference. */
        if(kernel_task_resources_ref(resources,table,waiter,
            KERNEL_RESOURCE_LOCK,true)!=KERNEL_RESOURCE_OK)
            return KERNEL_MUTEX_REJECTED;
        if(kernel_task_resources_ref(resources,table,waiter,
            KERNEL_RESOURCE_WAITER,false)!=KERNEL_RESOURCE_OK)
            return KERNEL_MUTEX_REJECTED;
        if(kernel_task_resources_ref(resources,table,current,
            KERNEL_RESOURCE_LOCK,false)!=KERNEL_RESOURCE_OK)
            return KERNEL_MUTEX_REJECTED;
        if(kernel_task_wake(table,waiter)!=KERNEL_TASK_OK)
            return KERNEL_MUTEX_REJECTED;
        lock->owner=waiter;
        lock->waiting=false;
        lock->waiter.slot=0;
        lock->waiter.generation=0;
        return KERNEL_MUTEX_OK;
    }
    if(kernel_task_resources_ref(resources,table,current,
        KERNEL_RESOURCE_LOCK,false)!=KERNEL_RESOURCE_OK)
        return KERNEL_MUTEX_REJECTED;
    lock->owned=false;
    lock->owner.slot=0;
    lock->owner.generation=0;
    return KERNEL_MUTEX_OK;
}
