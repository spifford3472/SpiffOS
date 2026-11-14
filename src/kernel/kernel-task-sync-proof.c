/* Purpose: M10-D5.2c bounded one-owner/one-waiter accounting proof.
 * Responsibilities: register lock/wait references with the authoritative
 * D5.2b task resource ledger, refuse unsafe ownership abandonment.
 * Dependencies: kernel-task-sync-proof.h and kernel-task-resources.c.
 * Invariants: no unregistered lock holder or waiter; no implicit task wake.
 * Concurrency: all calls require caller-owned IF=0 on one boot CPU; not IRQ.
 * References: ADR-m10-d5-supervisor-cleanup.md and AGENTS.md. */
#include "kernel-task-sync-proof.h"
#include <limits.h>

/* Handle equality uses the generation so a recycled slot cannot take over
 * a mutex originally owned by an earlier task in that same slot. */
static bool same(struct kernel_task_handle a,struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* A trusted caller first verifies that a task has a bound resource ledger
 * entry and remains in a nonterminal scheduling state. */
static bool live(const struct kernel_task_resources *resources,
                 const struct kernel_task_table *table,
                 struct kernel_task_handle handle)
{
    if(handle.slot>=KERNEL_TASK_CAPACITY) return false;
    const struct kernel_task_record *task=kernel_task_get(table,handle);
    const struct kernel_task_resource_record *r=
        &resources->task[handle.slot];
    return task!=NULL && task->state!=KERNEL_TASK_TERMINATED &&
        r->installed && !r->quarantined &&
        same(r->handle,handle);
}

/* Require zero initialized caller-owned state; no allocations or I/O. */
bool kernel_sync_proof_init(struct kernel_sync_proof_lock *lock)
{
    if(lock==NULL || lock->initialized) return false;
    lock->initialized=true;
    lock->owned=false;
    lock->waiting=false;
    lock->owner=(struct kernel_task_handle){0};
    lock->waiter=(struct kernel_task_handle){0};
    return true;
}

/* The lock manages a fixed owner and one waiter; the ledger is the source
 * of truth for whether cleanup can safely retire either task. */
enum kernel_sync_proof_result kernel_sync_proof_acquire(
    struct kernel_sync_proof_lock *lock,
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle caller)
{
    if(lock==NULL || !lock->initialized ||
       !kernel_task_resources_check(resources,table) ||
       !live(resources,table,caller))
        return KERNEL_SYNC_REJECTED;
    if(!lock->owned) {
        if(lock->waiting ||
           kernel_task_resources_ref(resources,table,caller,
             KERNEL_RESOURCE_LOCK,true)!=KERNEL_RESOURCE_OK)
            return KERNEL_SYNC_REJECTED;
        lock->owner=caller;
        lock->owned=true;
        return KERNEL_SYNC_OK;
    }
    if(same(lock->owner,caller) || lock->waiting ||
       !live(resources,table,lock->owner))
        return KERNEL_SYNC_REJECTED;
    if(kernel_task_resources_ref(resources,table,caller,
        KERNEL_RESOURCE_WAITER,true)!=KERNEL_RESOURCE_OK)
        return KERNEL_SYNC_REJECTED;
    lock->waiter=caller;
    lock->waiting=true;
    return KERNEL_SYNC_WAITING;
}

/* Preflight every affected reference so transfer cannot partially mutate
 * owner/waiter counts after one operation has already succeeded. */
enum kernel_sync_proof_result kernel_sync_proof_release(
    struct kernel_sync_proof_lock *lock,
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle caller)
{
    if(lock==NULL || !lock->initialized || !lock->owned ||
       !same(lock->owner,caller) ||
       !kernel_task_resources_check(resources,table) ||
       !live(resources,table,caller))
        return KERNEL_SYNC_REJECTED;
    struct kernel_task_resource_record *owner=&resources->task[caller.slot];
    if(owner->owned_locks==0) return KERNEL_SYNC_REJECTED;
    if(lock->waiting) {
        if(!live(resources,table,lock->waiter) ||
           same(lock->waiter,caller))
            return KERNEL_SYNC_REJECTED;
        struct kernel_task_resource_record *waiter=
            &resources->task[lock->waiter.slot];
        if(waiter->registered_waiters==0 || waiter->owned_locks==UINT_MAX)
            return KERNEL_SYNC_REJECTED;
        /* No intervening mutation on single CPU with IF=0: after this
         * preflight the three exact-reference adjustments cannot fail. */
        if(kernel_task_resources_ref(resources,table,lock->waiter,
             KERNEL_RESOURCE_LOCK,true)!=KERNEL_RESOURCE_OK ||
           kernel_task_resources_ref(resources,table,lock->waiter,
             KERNEL_RESOURCE_WAITER,false)!=KERNEL_RESOURCE_OK ||
           kernel_task_resources_ref(resources,table,caller,
             KERNEL_RESOURCE_LOCK,false)!=KERNEL_RESOURCE_OK)
            return KERNEL_SYNC_REJECTED;
        lock->owner=lock->waiter;
        lock->waiter=(struct kernel_task_handle){0};
        lock->waiting=false;
        return KERNEL_SYNC_OK;
    }
    if(kernel_task_resources_ref(resources,table,caller,
        KERNEL_RESOURCE_LOCK,false)!=KERNEL_RESOURCE_OK)
        return KERNEL_SYNC_REJECTED;
    lock->owner=(struct kernel_task_handle){0};
    lock->owned=false;
    return KERNEL_SYNC_OK;
}
