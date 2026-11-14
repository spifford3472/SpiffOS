/* Purpose: M10-D5.3 ranked, bounded multiwaiter mutex proof.
 * Responsibilities: enforce lock ordering; register M10-A BLOCKED/WAKE
 * and D5.2b references; calculate transitive priority donation.
 * Dependencies: kernel-sync-d53.h, authoritative M10-A and D5.2b.
 * Invariants: never create an extra task scheduler, free dead owner backing,
 * or release a lock on unknown owner termination.
 * Concurrency: boot CPU only, IF=0 across calls, never from IRQ.
 * References: ADR-m10-d53-bounded-synchronization.md and AGENTS.md. */
#include "kernel-sync-d53.h"
#include <limits.h>

/* Full-generation handle identity prevents recycled slots from inheriting
 * stale mutex rights. No pointer ownership or mutation. */
static bool same(struct kernel_task_handle a,struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* No memset: normal kernel is freestanding and has no C runtime library.
 * All fields are initialized individually in bounded loops. */
bool d53_init(struct d53_sync *sync)
{
    if(sync==NULL || sync->initialized) return false;
    for(size_t i=0;i<D53_LOCKS;++i) {
        sync->locks[i].owned=false;
        sync->locks[i].owner=(struct kernel_task_handle){0};
        sync->locks[i].waiting=0;
        for(size_t j=0;j<D53_TASKS;++j)
            sync->locks[i].waiters[j]=(struct kernel_task_handle){0};
    }
    for(size_t i=0;i<D53_TASKS;++i) {
        sync->profiles[i].installed=false;
        sync->profiles[i].handle=(struct kernel_task_handle){0};
        sync->profiles[i].base=KERNEL_PRIORITY_BACKGROUND;
    }
    sync->initialized=true;
    return true;
}

/* Profile is only a priority association, not a competing lifecycle.
 * A slot may be replaced after all lock and waiter references are gone. */
static bool member(const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table,struct kernel_task_handle h)
{
    if(h.slot>=D53_TASKS) return false;
    const struct kernel_task_record *task=kernel_task_get(table,h);
    const struct kernel_task_resource_record *r=&resources->task[h.slot];
    const struct d53_profile *p=&sync->profiles[h.slot];
    return task!=NULL && task->state!=KERNEL_TASK_TERMINATED &&
           r->installed && !r->quarantined && same(r->handle,h) &&
           p->installed && same(p->handle,h);
}

/* The pure rank comparison rejects all bits outside available ranks.
 * An existing held rank must be STRICTLY below the requested rank, which
 * makes circular wait impossible between clients obeying this API. */
static bool rank_allowed(unsigned rank,unsigned held_mask)
{
    if(rank>=D53_LOCKS || (held_mask & ~((1u<<D53_LOCKS)-1u))!=0)
        return false;
    return (held_mask & ~((1u<<rank)-1u))==0;
}

/* Calculate the exact ranked lock set currently held by this generation.
 * The caller checks global graph identity before using these bits. */
static unsigned held_mask(const struct d53_sync *sync,
                         struct kernel_task_handle task)
{
    unsigned result=0;
    for(unsigned i=0;i<D53_LOCKS;++i)
        if(sync->locks[i].owned && same(sync->locks[i].owner,task))
            result|=1u<<i;
    return result;
}

/* Authoritative identity/queue/ref invariants are checked before mutation.
 * Counter equality with this sidecar catches duplicate or detached owners.
 * Terminated owners remain present, preserving fail-closed orphan state. */
bool d53_check(const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table)
{
    if(sync==NULL || !sync->initialized ||
       !kernel_task_resources_check(resources,table)) return false;
    unsigned locks[D53_TASKS]={0};
    unsigned waits[D53_TASKS]={0};
    for(size_t i=0;i<D53_TASKS;++i)
        if(sync->profiles[i].installed &&
           (sync->profiles[i].handle.slot!=i ||
            sync->profiles[i].handle.generation==0 ||
            sync->profiles[i].base>KERNEL_PRIORITY_TIME_SENSITIVE))
            return false;
    for(size_t i=0;i<D53_LOCKS;++i) {
        const struct d53_lock *lock=&sync->locks[i];
        if(lock->waiting>D53_TASKS-1 || (!lock->owned && lock->waiting))
            return false;
        if(!lock->owned) continue;
        struct kernel_task_handle owner=lock->owner;
        if(owner.slot>=D53_TASKS ||
           !sync->profiles[owner.slot].installed ||
           !same(sync->profiles[owner.slot].handle,owner) ||
           kernel_task_get(table,owner)==NULL) return false;
        ++locks[owner.slot];
        for(size_t j=0;j<lock->waiting;++j) {
            struct kernel_task_handle w=lock->waiters[j];
            if(w.slot>=D53_TASKS || same(w,owner) ||
               !member(sync,resources,table,w) ||
               kernel_task_get(table,w)->state!=KERNEL_TASK_BLOCKED ||
               waits[w.slot]!=0) return false;
            ++waits[w.slot];
            /* A waiting task can hold only lower ranks, or we could
             * accidentally form a circular dependency at another lock. */
            if(!rank_allowed((unsigned)i,held_mask(sync,w)))
                return false;
        }
    }
    for(size_t i=0;i<D53_TASKS;++i)
        if(resources->task[i].owned_locks!=locks[i] ||
           resources->task[i].registered_waiters!=waits[i])
            return false;
    return true;
}

/* Enroll only an attached exact task; reject replacement while any mutex
 * still refers to its slot. No task creation, scheduling or allocation. */
enum d53_result d53_enroll(struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle h,enum kernel_priority_class priority)
{
    if(sync==NULL || !sync->initialized || h.slot>=D53_TASKS ||
       priority>KERNEL_PRIORITY_TIME_SENSITIVE ||
       !kernel_task_resources_check(resources,table))
        return D53_REJECTED;
    const struct kernel_task_record *task=kernel_task_get(table,h);
    if(task==NULL || task->state==KERNEL_TASK_TERMINATED ||
       !resources->task[h.slot].installed ||
       !same(resources->task[h.slot].handle,h) ||
       resources->task[h.slot].quarantined)
        return D53_REJECTED;
    for(size_t i=0;i<D53_LOCKS;++i) {
        const struct d53_lock *lock=&sync->locks[i];
        if(lock->owned && lock->owner.slot==h.slot &&
           !same(lock->owner,h)) return D53_REJECTED;
        for(size_t j=0;j<lock->waiting;++j)
            if(lock->waiters[j].slot==h.slot &&
               !same(lock->waiters[j],h)) return D53_REJECTED;
    }
    if(sync->profiles[h.slot].installed &&
       same(sync->profiles[h.slot].handle,h))
        return D53_REJECTED;
    sync->profiles[h.slot]=(struct d53_profile){
        .installed=true,.handle=h,.base=priority};
    return D53_OK;
}

/* Bounded fixed-point propagation computes maximum donations along owner
 * edges. Ranks ensure a donor can wait only above locks it already holds;
 * at most D53_TASKS sweeps suffice for all such acyclic paths. */
static void effective_all(const struct d53_sync *sync,
                          enum kernel_priority_class value[D53_TASKS])
{
    for(size_t i=0;i<D53_TASKS;++i)
        value[i]=sync->profiles[i].installed ?
            sync->profiles[i].base:KERNEL_PRIORITY_BACKGROUND;
    for(size_t sweep=0;sweep<D53_TASKS;++sweep) {
        bool changed=false;
        for(size_t i=0;i<D53_LOCKS;++i) {
            const struct d53_lock *lock=&sync->locks[i];
            if(!lock->owned) continue;
            for(size_t j=0;j<lock->waiting;++j) {
                size_t owner=lock->owner.slot,waiter=lock->waiters[j].slot;
                if(value[waiter]>value[owner]) {
                    value[owner]=value[waiter];
                    changed=true;
                }
            }
        }
        if(!changed) break;
    }
}

/* Read-only donation. It is policy input for a future scheduler; M10-A
 * remains a FIFO and no CPU dispatch is silently reprioritized. */
enum d53_result d53_effective(const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle task,enum kernel_priority_class *out)
{
    if(out==NULL || !d53_check(sync,resources,table) ||
       !member(sync,resources,table,task))
        return D53_REJECTED;
    enum kernel_priority_class values[D53_TASKS];
    effective_all(sync,values);
    *out=values[task.slot];
    return D53_OK;
}

/* Only the running task may acquire. Rank ordering and one outstanding
 * wait are checked before touching either table. The caller must switch
 * away on D53_BLOCKED: changing policy is not switching CPU context. */
enum d53_result d53_acquire(struct d53_sync *sync,
    struct kernel_task_resources *resources,struct kernel_task_table *table,
    unsigned rank,struct kernel_task_handle current)
{
    if(rank>=D53_LOCKS || !d53_check(sync,resources,table) ||
       !member(sync,resources,table,current) ||
       table->current_slot!=current.slot ||
       kernel_task_get(table,current)->state!=KERNEL_TASK_RUNNING ||
       !rank_allowed(rank,held_mask(sync,current)))
        return D53_REJECTED;
    for(size_t i=0;i<D53_LOCKS;++i)
        for(size_t j=0;j<sync->locks[i].waiting;++j)
            if(same(sync->locks[i].waiters[j],current))
                return D53_REJECTED;
    struct d53_lock *lock=&sync->locks[rank];
    if(!lock->owned) {
        if(resources->task[current.slot].owned_locks==UINT_MAX ||
           kernel_task_resources_ref(resources,table,current,
               KERNEL_RESOURCE_LOCK,true)!=KERNEL_RESOURCE_OK)
            return D53_REJECTED;
        lock->owned=true;
        lock->owner=current;
        return D53_OK;
    }
    const struct kernel_task_record *owner=kernel_task_get(table,lock->owner);
    if(owner==NULL || owner->state==KERNEL_TASK_TERMINATED)
        return D53_ORPHANED;
    if(lock->waiting>=D53_TASKS-1 ||
       resources->task[current.slot].registered_waiters==UINT_MAX)
        return D53_REJECTED;
    if(kernel_task_resources_ref(resources,table,current,
           KERNEL_RESOURCE_WAITER,true)!=KERNEL_RESOURCE_OK)
        return D53_REJECTED;
    if(kernel_task_block(table,current)!=KERNEL_TASK_OK) {
        (void)kernel_task_resources_ref(resources,table,current,
            KERNEL_RESOURCE_WAITER,false);
        return D53_REJECTED;
    }
    lock->waiters[lock->waiting++]=current;
    return D53_BLOCKED;
}

/* A current exact owner wakes one highest effective-priority waiter.
 * On an equal priority, the oldest registered waiter wins. Single-CPU
 * IF=0 makes the validated ledger changes and M10-A wake indivisible. */
enum d53_result d53_release(struct d53_sync *sync,
    struct kernel_task_resources *resources,struct kernel_task_table *table,
    unsigned rank,struct kernel_task_handle current)
{
    if(rank>=D53_LOCKS || !d53_check(sync,resources,table))
        return D53_REJECTED;
    struct d53_lock *lock=&sync->locks[rank];
    if(!lock->owned || !same(lock->owner,current))
        return D53_REJECTED;
    if(!member(sync,resources,table,current) ||
       table->current_slot!=current.slot ||
       kernel_task_get(table,current)->state!=KERNEL_TASK_RUNNING ||
       resources->task[current.slot].owned_locks==0)
        return D53_REJECTED;
    if(lock->waiting==0) {
        if(kernel_task_resources_ref(resources,table,current,
            KERNEL_RESOURCE_LOCK,false)!=KERNEL_RESOURCE_OK)
            return D53_REJECTED;
        lock->owned=false;
        lock->owner=(struct kernel_task_handle){0};
        return D53_OK;
    }
    enum kernel_priority_class value[D53_TASKS];
    effective_all(sync,value);
    size_t selected=0;
    for(size_t i=1;i<lock->waiting;++i)
        if(value[lock->waiters[i].slot]>value[lock->waiters[selected].slot])
            selected=i;
    struct kernel_task_handle winner=lock->waiters[selected];
    if(!member(sync,resources,table,winner) ||
       kernel_task_get(table,winner)->state!=KERNEL_TASK_BLOCKED ||
       resources->task[winner.slot].registered_waiters==0 ||
       resources->task[winner.slot].owned_locks==UINT_MAX ||
       table->ready_count>=KERNEL_TASK_CAPACITY)
        return D53_REJECTED;
    /* With verified count bounds and IF=0, none of these mutations can
     * fail. If future APIs gain failure modes, adopt explicit rollback. */
    if(kernel_task_resources_ref(resources,table,winner,
        KERNEL_RESOURCE_LOCK,true)!=KERNEL_RESOURCE_OK ||
       kernel_task_resources_ref(resources,table,winner,
        KERNEL_RESOURCE_WAITER,false)!=KERNEL_RESOURCE_OK ||
       kernel_task_resources_ref(resources,table,current,
        KERNEL_RESOURCE_LOCK,false)!=KERNEL_RESOURCE_OK ||
       kernel_task_wake(table,winner)!=KERNEL_TASK_OK)
        return D53_REJECTED;
    for(size_t i=selected+1;i<lock->waiting;++i)
        lock->waiters[i-1]=lock->waiters[i];
    --lock->waiting;
    lock->owner=winner;
    return D53_OK;
}

/* Supervisor/dispatcher cancels an exact still-blocked waiter and wakes it
 * BEFORE termination, allowing ordinary M10-A lifetime rules to apply. */
enum d53_result d53_cancel(struct d53_sync *sync,
    struct kernel_task_resources *resources,struct kernel_task_table *table,
    unsigned rank,struct kernel_task_handle waiter)
{
    if(rank>=D53_LOCKS || !d53_check(sync,resources,table) ||
       (table->current_slot!=SIZE_MAX &&
        table->current_slot!=KERNEL_TASK_SUPERVISOR_SLOT))
        return D53_REJECTED;
    struct d53_lock *lock=&sync->locks[rank];
    if(!lock->owned || !member(sync,resources,table,waiter) ||
       kernel_task_get(table,waiter)->state!=KERNEL_TASK_BLOCKED ||
       resources->task[waiter.slot].registered_waiters==0 ||
       table->ready_count>=KERNEL_TASK_CAPACITY)
        return D53_REJECTED;
    size_t selected=lock->waiting;
    for(size_t i=0;i<lock->waiting;++i)
        if(same(waiter,lock->waiters[i])) selected=i;
    if(selected==lock->waiting) return D53_REJECTED;
    if(kernel_task_resources_ref(resources,table,waiter,
        KERNEL_RESOURCE_WAITER,false)!=KERNEL_RESOURCE_OK ||
       kernel_task_wake(table,waiter)!=KERNEL_TASK_OK)
        return D53_REJECTED;
    for(size_t i=selected+1;i<lock->waiting;++i)
        lock->waiters[i-1]=lock->waiters[i];
    --lock->waiting;
    return D53_OK;
}

/* Never release a dead owner's lock: expose its state so the caller can
 * quarantine the dependency until separate validated recovery exists. */
enum d53_result d53_orphan(const struct d53_sync *sync,
    const struct kernel_task_table *table,unsigned rank)
{
    if(sync==NULL || !sync->initialized || rank>=D53_LOCKS ||
       !kernel_task_check(table)) return D53_REJECTED;
    const struct d53_lock *lock=&sync->locks[rank];
    if(!lock->owned) return D53_OK;
    const struct kernel_task_record *owner=kernel_task_get(table,lock->owner);
    if(owner==NULL || owner->state==KERNEL_TASK_TERMINATED)
        return D53_ORPHANED;
    return D53_OK;
}
