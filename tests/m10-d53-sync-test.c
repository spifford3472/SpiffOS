/* Purpose: M10-D5.3 hosted multiwaiter/donation/rank regression.
 * Responsibilities: exercise exact M10-A task transitions and registered
 * D5.2b lock/wait refs, priority donation and fail-closed orphan behavior.
 * Dependencies: kernel-sync-d53.h and authoritative task/reaper modules.
 * Invariants: no stale ownership, no automatic dead-owner releases; all
 * runnable/blocked status derived from M10-A, not a second task table.
 * Concurrency: single-CPU hosted model of IF=0 serialized operations.
 * Reference: ADR-m10-d53-bounded-synchronization.md and AGENTS.md. */
#include "../src/kernel/kernel-sync-d53.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned released;
static bool release_owner(size_t index)
{
    (void)index;
    ++released;
    return true;
}

/* Build an exact stack-bound task, then attach priority to the same handle. */
static struct kernel_task_handle create(struct kernel_task_table *table,
    struct kernel_task_resources *r,struct d53_sync *sync,
    size_t owner,enum kernel_priority_class base)
{
    struct kernel_task_handle h;
    assert(kernel_task_create(table,false,&h)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding b={
        .bottom=(uintptr_t)0x100000u+owner*0x10000u,
        .bytes=16384,
        .saved_rsp=(uintptr_t)0x103fe0u+owner*0x10000u,
        .owner_index=owner};
    assert(kernel_task_resources_bind(r,table,h,&b)==KERNEL_RESOURCE_OK);
    assert(d53_enroll(sync,r,table,h,base)==D53_OK);
    assert(kernel_task_make_runnable(table,h)==KERNEL_TASK_OK);
    return h;
}

/* Real table dispatch; a blocked task is never returned by this API. */
static struct kernel_task_handle dispatch(struct kernel_task_table *table)
{
    struct kernel_task_handle h;
    assert(kernel_task_dispatch(table,&h)==KERNEL_TASK_OK);
    return h;
}
static void finish(struct kernel_task_table *table,
    struct kernel_task_resources *r,struct kernel_task_handle h)
{
    assert(kernel_task_terminate(table,h)==KERNEL_TASK_OK);
    assert(kernel_task_resources_retire(r,table,h,(uintptr_t)0x400000u,
        release_owner)==KERNEL_RESOURCE_OK);
}

/* Three contenders: highest effective priority receives exact wake,
 * lower-priority waiter remains BLOCKED and its reference remains held. */
static void multiple_waiters(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s));
    struct kernel_task_handle a=create(&t,&r,&s,0,KERNEL_PRIORITY_BACKGROUND);
    struct kernel_task_handle b=create(&t,&r,&s,1,KERNEL_PRIORITY_NORMAL);
    struct kernel_task_handle c=create(&t,&r,&s,2,KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(dispatch(&t).slot==a.slot);
    assert(d53_acquire(&s,&r,&t,0,a)==D53_OK);
    assert(kernel_task_yield(&t,a)==KERNEL_TASK_OK);
    assert(dispatch(&t).slot==b.slot);
    assert(d53_acquire(&s,&r,&t,0,b)==D53_BLOCKED);
    assert(dispatch(&t).slot==c.slot);
    assert(d53_acquire(&s,&r,&t,0,c)==D53_BLOCKED);
    enum kernel_priority_class effective=KERNEL_PRIORITY_BACKGROUND;
    assert(d53_effective(&s,&r,&t,a,&effective)==D53_OK);
    assert(effective==KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(t.ready_count==1 && t.tasks[b.slot].state==KERNEL_TASK_BLOCKED &&
           t.tasks[c.slot].state==KERNEL_TASK_BLOCKED);
    assert(dispatch(&t).slot==a.slot);
    assert(d53_release(&s,&r,&t,0,a)==D53_OK);
    assert(s.locks[0].owner.slot==c.slot && s.locks[0].waiting==1);
    assert(t.tasks[c.slot].state==KERNEL_TASK_RUNNABLE &&
           t.tasks[b.slot].state==KERNEL_TASK_BLOCKED);
    assert(d53_check(&s,&r,&t));
    assert(kernel_task_yield(&t,a)==KERNEL_TASK_OK);
    assert(dispatch(&t).slot==c.slot);
    assert(d53_release(&s,&r,&t,0,c)==D53_OK);
    assert(s.locks[0].owner.slot==b.slot && s.locks[0].waiting==0);
    assert(kernel_task_yield(&t,c)==KERNEL_TASK_OK);
    assert(dispatch(&t).slot==a.slot);
    finish(&t,&r,a);
    assert(dispatch(&t).slot==b.slot);
    assert(d53_release(&s,&r,&t,0,b)==D53_OK);
    finish(&t,&r,b);
    assert(dispatch(&t).slot==c.slot);
    finish(&t,&r,c);
    assert(d53_check(&s,&r,&t));
}

/* Bounded chain: high-priority C waiting on B's rank0 donates to B;
 * B waiting on A's rank1 in turn donates transitively to A. */
static void donation_and_order(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s));
    struct kernel_task_handle a=create(&t,&r,&s,0,KERNEL_PRIORITY_BACKGROUND);
    struct kernel_task_handle b=create(&t,&r,&s,1,KERNEL_PRIORITY_NORMAL);
    struct kernel_task_handle c=create(&t,&r,&s,2,KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(dispatch(&t).slot==a.slot);
    assert(d53_acquire(&s,&r,&t,1,a)==D53_OK);
    assert(kernel_task_yield(&t,a)==KERNEL_TASK_OK);
    assert(dispatch(&t).slot==b.slot);
    assert(d53_acquire(&s,&r,&t,0,b)==D53_OK);
    assert(d53_acquire(&s,&r,&t,1,b)==D53_BLOCKED);
    assert(dispatch(&t).slot==c.slot);
    assert(d53_acquire(&s,&r,&t,0,c)==D53_BLOCKED);
    enum kernel_priority_class p=KERNEL_PRIORITY_BACKGROUND;
    assert(d53_effective(&s,&r,&t,a,&p)==D53_OK &&
           p==KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(d53_effective(&s,&r,&t,b,&p)==D53_OK &&
           p==KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(dispatch(&t).slot==a.slot);
    struct d53_sync snapshot=s;
    struct kernel_task_resources prior=r;
    struct kernel_task_table before=t;
    assert(d53_acquire(&s,&r,&t,0,a)==D53_REJECTED);
    assert(memcmp(&snapshot,&s,sizeof s)==0);
    assert(memcmp(&prior,&r,sizeof r)==0);
    assert(memcmp(&before,&t,sizeof t)==0);
    assert(d53_release(&s,&r,&t,1,a)==D53_OK);
    assert(kernel_task_yield(&t,a)==KERNEL_TASK_OK);
    assert(dispatch(&t).slot==b.slot);
    assert(d53_release(&s,&r,&t,1,b)==D53_OK);
    assert(d53_release(&s,&r,&t,0,b)==D53_OK);
    assert(kernel_task_yield(&t,b)==KERNEL_TASK_OK);
    assert(dispatch(&t).slot==a.slot);
    finish(&t,&r,a);
    assert(dispatch(&t).slot==c.slot);
    assert(d53_release(&s,&r,&t,0,c)==D53_OK);
    finish(&t,&r,c);
    assert(dispatch(&t).slot==b.slot);
    finish(&t,&r,b);
    assert(d53_check(&s,&r,&t));
}

/* Dispatcher can revoke exact BLOCKED waiter before termination; owner
 * termination itself quarantines a live lock rather than auto-unlocking. */
static void cancel_and_orphan(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s));
    struct kernel_task_handle a=create(&t,&r,&s,0,KERNEL_PRIORITY_NORMAL);
    struct kernel_task_handle b=create(&t,&r,&s,1,KERNEL_PRIORITY_INTERACTIVE);
    assert(dispatch(&t).slot==a.slot);
    assert(d53_acquire(&s,&r,&t,0,a)==D53_OK);
    assert(kernel_task_yield(&t,a)==KERNEL_TASK_OK);
    assert(dispatch(&t).slot==b.slot);
    assert(d53_acquire(&s,&r,&t,0,b)==D53_BLOCKED);
    assert(d53_cancel(&s,&r,&t,0,b)==D53_OK);
    assert(d53_cancel(&s,&r,&t,0,b)==D53_REJECTED);
    assert(t.tasks[b.slot].state==KERNEL_TASK_RUNNABLE);
    assert(r.task[b.slot].registered_waiters==0);
    assert(dispatch(&t).slot==a.slot);
    assert(kernel_task_terminate(&t,a)==KERNEL_TASK_OK);
    assert(d53_orphan(&s,&t,0)==D53_ORPHANED);
    assert(d53_check(&s,&r,&t));
    assert(kernel_task_resources_retire(&r,&t,a,(uintptr_t)0x400000u,
        release_owner)==KERNEL_RESOURCE_BLOCKED);
    assert(dispatch(&t).slot==b.slot);
    assert(d53_acquire(&s,&r,&t,0,b)==D53_ORPHANED);
    finish(&t,&r,b);
    assert(s.locks[0].owned && s.locks[0].owner.slot==a.slot);
}

/* Single uncontended cycle under sanitizer; tasks are physically reaped
 * and slots generationally reused for 20,000 complete lock lifetimes. */
static void stress(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s));
    struct kernel_task_handle previous={UINT32_MAX,0};
    for(unsigned i=0;i<20000;++i) {
        struct kernel_task_handle a=create(&t,&r,&s,0,KERNEL_PRIORITY_NORMAL);
        if(i) assert(a.slot==previous.slot &&
                      a.generation!=previous.generation);
        assert(dispatch(&t).slot==a.slot);
        assert(d53_acquire(&s,&r,&t,0,a)==D53_OK);
        assert(d53_acquire(&s,&r,&t,0,a)==D53_REJECTED);
        assert(d53_release(&s,&r,&t,0,a)==D53_OK);
        finish(&t,&r,a);
        assert(d53_check(&s,&r,&t));
        previous=a;
    }
}

/* Fill the entire ordinary task capacity: one owner plus fourteen
 * blocked waiters; cancellation retains the exact owner reference. */
static void full_capacity(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    struct kernel_task_handle task[D53_TASKS];
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s));
    for(size_t i=0;i<D53_TASKS;++i)
        task[i]=create(&t,&r,&s,i,KERNEL_PRIORITY_NORMAL);
    assert(dispatch(&t).slot==task[0].slot);
    assert(d53_acquire(&s,&r,&t,0,task[0])==D53_OK);
    assert(kernel_task_yield(&t,task[0])==KERNEL_TASK_OK);
    for(size_t i=1;i<D53_TASKS;++i) {
        assert(dispatch(&t).slot==task[i].slot);
        assert(d53_acquire(&s,&r,&t,0,task[i])==D53_BLOCKED);
        assert(t.tasks[task[i].slot].state==KERNEL_TASK_BLOCKED);
        assert(d53_check(&s,&r,&t));
    }
    assert(s.locks[0].waiting==D53_TASKS-1);
    assert(t.ready_count==1 && t.current_slot==SIZE_MAX);
    struct kernel_task_handle stale=task[1];
    ++stale.generation;
    assert(d53_cancel(&s,&r,&t,0,stale)==D53_REJECTED);
    for(size_t i=1;i<D53_TASKS;++i) {
        assert(d53_cancel(&s,&r,&t,0,task[i])==D53_OK);
        assert(r.task[task[i].slot].registered_waiters==0);
    }
    assert(s.locks[0].waiting==0 && d53_check(&s,&r,&t));
    assert(dispatch(&t).slot==task[0].slot);
    assert(d53_release(&s,&r,&t,0,task[0])==D53_OK);
    finish(&t,&r,task[0]);
    for(size_t i=1;i<D53_TASKS;++i) {
        assert(dispatch(&t).slot==task[i].slot);
        finish(&t,&r,task[i]);
    }
    assert(d53_check(&s,&r,&t));
}

int main(void)
{
    full_capacity();
    multiple_waiters();
    donation_and_order();
    cancel_and_orphan();
    stress();
    puts("SPIFFOS_M10_D53_MULTIWAITER_OK");
    return 0;
}
