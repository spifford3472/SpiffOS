/* Purpose: D5.2d M10-A mutex blocking/waking acceptance and stress.
 * Responsibilities: test registered lock/wait refs, actual M10-A BLOCKED
 * and FIFO wake transitions, rejected owner death and generation reuse.
 * Dependencies: kernel-task-mutex.h, task-resources.c, task-reaper.c.
 * Invariants: no mutation on invalid calls; no blocked waiter dispatch;
 * physical owner release follows verified termination and zero references.
 * Concurrency: hosted single CPU models IF=0 serialization, not IRQ.
 * References: ADR-m10-d5-mutex-block-wake.md and AGENTS.md. */
#include "../src/kernel/kernel-task-mutex.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned releases;
static bool release_stack(size_t owner_index)
{
    (void)owner_index;
    ++releases;
    return true;
}

/* Construct owned 16KiB task stacks entirely in synthetic address space.
 * The resource ledger keeps backing tokens separate from task slot numbers. */
static struct kernel_task_handle created(struct kernel_task_table *t,
    struct kernel_task_resources *r,size_t owner)
{
    struct kernel_task_handle h;
    assert(kernel_task_create(t,false,&h)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding b={
        .bottom=(uintptr_t)0x100000u+owner*0x10000u,
        .bytes=16384u,
        .saved_rsp=(uintptr_t)0x103fe0u+owner*0x10000u,
        .owner_index=owner
    };
    assert(kernel_task_resources_bind(r,t,h,&b)==KERNEL_RESOURCE_OK);
    assert(kernel_task_make_runnable(t,h)==KERNEL_TASK_OK);
    return h;
}
static struct kernel_task_handle next(struct kernel_task_table *t)
{
    struct kernel_task_handle h;
    assert(kernel_task_dispatch(t,&h)==KERNEL_TASK_OK);
    return h;
}
static bool same(struct kernel_task_handle a,struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* Run both tasks through the real table, including a blocked waiter and
 * explicit dispatcher handoff. No CPU registers are switched in this test. */
static void contested_cycle(struct kernel_task_table *t,
    struct kernel_task_resources *r,struct kernel_task_mutex *lock,
    unsigned i)
{
    struct kernel_task_handle a=created(t,r,0);
    struct kernel_task_handle b=created(t,r,1);
    struct kernel_task_handle running=next(t);
    assert(same(running,a));
    assert(kernel_task_mutex_acquire(lock,r,t,a)==KERNEL_MUTEX_OK);
    assert(r->task[a.slot].owned_locks==1);
    assert(kernel_task_yield(t,a)==KERNEL_TASK_OK);
    running=next(t);
    assert(same(running,b));
    assert(kernel_task_mutex_acquire(lock,r,t,b)==KERNEL_MUTEX_BLOCKED);
    assert(t->tasks[b.slot].state==KERNEL_TASK_BLOCKED);
    assert(t->current_slot==SIZE_MAX);
    assert(r->task[b.slot].registered_waiters==1);
    assert(kernel_task_check(t));
    running=next(t);
    assert(same(running,a));
    /* The blocked task is absent from the ready queue and cannot
     * execute until its exact-generation owner calls wake. */
    assert(t->ready_count==0);
    struct kernel_task_table snapshot=*t;
    struct kernel_task_resources snapshot_r=*r;
    struct kernel_task_mutex snapshot_lock=*lock;
    assert(kernel_task_mutex_acquire(lock,r,t,a)==KERNEL_MUTEX_REJECTED);
    assert(memcmp(t,&snapshot,sizeof *t)==0);
    assert(memcmp(r,&snapshot_r,sizeof *r)==0);
    assert(memcmp(lock,&snapshot_lock,sizeof *lock)==0);
    assert(kernel_task_mutex_release(lock,r,t,a)==KERNEL_MUTEX_OK);
    assert(t->tasks[b.slot].state==KERNEL_TASK_RUNNABLE);
    assert(r->task[a.slot].owned_locks==0);
    assert(r->task[b.slot].owned_locks==1 &&
           r->task[b.slot].registered_waiters==0);
    assert(kernel_task_yield(t,a)==KERNEL_TASK_OK);
    running=next(t);
    assert(same(running,b));
    assert(kernel_task_mutex_release(lock,r,t,b)==KERNEL_MUTEX_OK);
    assert(kernel_task_terminate(t,b)==KERNEL_TASK_OK);
    running=next(t);
    assert(same(running,a));
    assert(kernel_task_terminate(t,a)==KERNEL_TASK_OK);
    assert(t->current_slot==SIZE_MAX && lock->owned==false);
    unsigned before=releases;
    assert(kernel_task_resources_retire(r,t,b,(uintptr_t)0x400000u,
        release_stack)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_retire(r,t,a,(uintptr_t)0x400000u,
        release_stack)==KERNEL_RESOURCE_OK);
    assert(releases-before==2);
    assert(kernel_task_resources_check(r,t));
    (void)i;
}

static void owner_death_denied(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct kernel_task_mutex l={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r));
    assert(kernel_task_mutex_init(&l));
    struct kernel_task_handle a=created(&t,&r,0);
    struct kernel_task_handle b=created(&t,&r,1);
    assert(same(next(&t),a));
    assert(kernel_task_mutex_acquire(&l,&r,&t,a)==KERNEL_MUTEX_OK);
    assert(kernel_task_yield(&t,a)==KERNEL_TASK_OK);
    assert(same(next(&t),b));
    assert(kernel_task_mutex_acquire(&l,&r,&t,b)==KERNEL_MUTEX_BLOCKED);
    assert(same(next(&t),a));
    assert(kernel_task_terminate(&t,a)==KERNEL_TASK_OK);
    struct kernel_task_resources before_r=r;
    struct kernel_task_table before_t=t;
    struct kernel_task_mutex before_l=l;
    assert(kernel_task_mutex_release(&l,&r,&t,a)==KERNEL_MUTEX_REJECTED);
    assert(memcmp(&r,&before_r,sizeof r)==0 &&
           memcmp(&t,&before_t,sizeof t)==0 &&
           memcmp(&l,&before_l,sizeof l)==0);
    assert(kernel_task_resources_retire(&r,&t,a,(uintptr_t)0x400000u,
        release_stack)==KERNEL_RESOURCE_BLOCKED);
    assert(r.task[b.slot].registered_waiters==1);
}

static void stale_wrong_and_waiter_death(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct kernel_task_mutex l={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r));
    assert(kernel_task_mutex_init(&l));
    struct kernel_task_handle a=created(&t,&r,0);
    struct kernel_task_handle b=created(&t,&r,1);
    assert(same(next(&t),a));
    assert(kernel_task_mutex_acquire(&l,&r,&t,a)==KERNEL_MUTEX_OK);
    struct kernel_task_handle forged=a;
    ++forged.generation;
    assert(kernel_task_mutex_release(&l,&r,&t,forged)==KERNEL_MUTEX_REJECTED);
    assert(kernel_task_yield(&t,a)==KERNEL_TASK_OK);
    assert(same(next(&t),b));
    assert(kernel_task_mutex_release(&l,&r,&t,b)==KERNEL_MUTEX_REJECTED);
    assert(kernel_task_mutex_acquire(&l,&r,&t,b)==KERNEL_MUTEX_BLOCKED);
    assert(same(next(&t),a));
    assert(kernel_task_terminate(&t,b)==KERNEL_TASK_OK);
    struct kernel_task_resources before_r=r;
    struct kernel_task_table before_t=t;
    struct kernel_task_mutex before_l=l;
    assert(kernel_task_mutex_release(&l,&r,&t,a)==KERNEL_MUTEX_REJECTED);
    assert(memcmp(&r,&before_r,sizeof r)==0 &&
           memcmp(&t,&before_t,sizeof t)==0 &&
           memcmp(&l,&before_l,sizeof l)==0);
    assert(r.task[b.slot].registered_waiters==1);
}

int main(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct kernel_task_mutex l={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r));
    assert(kernel_task_mutex_init(&l));
    for(unsigned i=0;i<20000;++i)
        contested_cycle(&t,&r,&l,i);
    owner_death_denied();
    stale_wrong_and_waiter_death();
    puts("SPIFFOS_M10_D5_MUTEX_BLOCK_WAKE_OK");
    return 0;
}
