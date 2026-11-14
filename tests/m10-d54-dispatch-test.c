/* Purpose: M10-D5.4 effective priority and authoritative dispatch regression.
 * Responsibilities: verify donated priority, fairness, supervisor reserve,
 * stale handles, and exact task table mutation with sanitizer stress.
 * Dependencies: D5.3 sidecar, M10-A selected dispatch, D5.2b ledger.
 * Invariants: only authoritative M10-A can become RUNNING; no implicit
 * stack switch, no IRQ, no release of orphaned owners.
 * Reference: ADR-m10-d54-priority-dispatch.md and AGENTS.md. */
#include "../src/kernel/kernel-dispatch-d54.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static bool release_owner(size_t owner_index) {(void)owner_index;return true;}

/* Attach a unique 16KiB synthetic owner stack before enrollment. */
static struct kernel_task_handle create(struct kernel_task_table *table,
    struct kernel_task_resources *resources,struct d53_sync *sync,
    size_t owner,enum kernel_priority_class priority)
{
    struct kernel_task_handle h={0};
    assert(kernel_task_create(table,false,&h)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding b={
       .bottom=(uintptr_t)0x100000u+owner*0x10000u,
       .bytes=16384,.saved_rsp=(uintptr_t)0x103fe0u+owner*0x10000u,
       .owner_index=owner};
    assert(kernel_task_resources_bind(resources,table,h,&b)==KERNEL_RESOURCE_OK);
    assert(d53_enroll(sync,resources,table,h,priority)==D53_OK);
    assert(kernel_task_make_runnable(table,h)==KERNEL_TASK_OK);
    return h;
}
static struct kernel_task_handle dispatch(struct d54_dispatch *policy,
    struct d53_sync *sync,struct kernel_task_resources *resources,
    struct kernel_task_table *table)
{
    struct kernel_task_handle h={0};
    assert(d54_dispatch_next(policy,sync,resources,table,&h)==KERNEL_TASK_OK);
    assert(table->current_slot==h.slot);
    return h;
}
static bool same(struct kernel_task_handle a,struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}
/* A lower base priority lock owner gets high-priority effective donation,
 * and the authoritative selected dispatch runs it ahead of a medium CPU hog.
 * When its lock is released, that donation disappears. */
static void donation_dispatch(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    struct d54_dispatch d={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s) && d54_init(&d));
    struct kernel_task_handle low=create(&t,&r,&s,0,KERNEL_PRIORITY_BACKGROUND);
    assert(same(dispatch(&d,&s,&r,&t),low));
    assert(d53_acquire(&s,&r,&t,0,low)==D53_OK);
    assert(kernel_task_yield(&t,low)==KERNEL_TASK_OK);
    struct kernel_task_handle medium=create(&t,&r,&s,1,KERNEL_PRIORITY_NORMAL);
    struct kernel_task_handle high=create(&t,&r,&s,2,KERNEL_PRIORITY_TIME_SENSITIVE);
    /* Select high first and block it on low; medium remains ready. */
    assert(same(dispatch(&d,&s,&r,&t),high));
    assert(d53_acquire(&s,&r,&t,0,high)==D53_BLOCKED);
    struct kernel_task_handle selected=dispatch(&d,&s,&r,&t);
    assert(same(selected,low));
    enum kernel_priority_class effective=KERNEL_PRIORITY_BACKGROUND;
    assert(d53_effective(&s,&r,&t,low,&effective)==D53_OK);
    assert(effective==KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(d53_release(&s,&r,&t,0,low)==D53_OK);
    assert(d53_effective(&s,&r,&t,low,&effective)==D53_OK);
    assert(effective==KERNEL_PRIORITY_BACKGROUND);
    assert(kernel_task_yield(&t,low)==KERNEL_TASK_OK);
    assert(same(dispatch(&d,&s,&r,&t),high));
    assert(d53_release(&s,&r,&t,0,high)==D53_OK);
    assert(kernel_task_terminate(&t,high)==KERNEL_TASK_OK);
    assert(kernel_task_resources_retire(&r,&t,high,(uintptr_t)0x400000,
        release_owner)==KERNEL_RESOURCE_OK);
    /* Drain remaining two active tasks without changing their identity. */
    for(unsigned k=0;k<2;++k) {
        struct kernel_task_handle h=dispatch(&d,&s,&r,&t);
        assert(same(h,medium)||same(h,low));
        assert(kernel_task_terminate(&t,h)==KERNEL_TASK_OK);
        assert(kernel_task_resources_retire(&r,&t,h,(uintptr_t)0x400000,
            release_owner)==KERNEL_RESOURCE_OK);
    }
}
/* Task 15 is reserved. If supervisor remains ready it must run at least
 * once in each four dispatch opportunities, regardless of worker load. */
static void supervisor_budget(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    struct d54_dispatch d={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s) && d54_init(&d));
    for(size_t i=0;i<4;++i)
        (void)create(&t,&r,&s,i,KERNEL_PRIORITY_TIME_SENSITIVE);
    struct kernel_task_handle supervisor={0};
    assert(kernel_task_create(&t,true,&supervisor)==KERNEL_TASK_OK);
    assert(supervisor.slot==KERNEL_TASK_SUPERVISOR_SLOT);
    assert(kernel_task_make_runnable(&t,supervisor)==KERNEL_TASK_OK);
    size_t since=0,supervisor_runs=0;
    for(size_t i=0;i<20000;++i) {
        struct kernel_task_handle h=dispatch(&d,&s,&r,&t);
        if(same(h,supervisor)) {
            assert(since<=4);
            since=0;
            ++supervisor_runs;
        } else ++since;
        assert(kernel_task_yield(&t,h)==KERNEL_TASK_OK);
    }
    assert(supervisor_runs>=5000);
    /* Forged generation cannot run even with a matching numerical slot. */
    struct kernel_task_handle stale=supervisor;
    ++stale.generation;
    struct kernel_task_table snapshot=t;
    struct kernel_task_handle unchanged={UINT32_MAX,UINT32_MAX};
    assert(kernel_task_dispatch_selected(&t,stale,&unchanged)==KERNEL_TASK_BAD_HANDLE);
    assert(memcmp(&snapshot,&t,sizeof t)==0);
    assert(unchanged.slot==UINT32_MAX);
}
/* Even with a continuously runnable high-priority worker, old ready-age
 * eventually selects a lower-priority task. No raw slot ties survive reuse. */
static void fairness_and_stale_profile(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    struct d54_dispatch d={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s) && d54_init(&d));
    struct kernel_task_handle high=create(&t,&r,&s,0,KERNEL_PRIORITY_TIME_SENSITIVE);
    struct kernel_task_handle low=create(&t,&r,&s,1,KERNEL_PRIORITY_BACKGROUND);
    bool seen_low=false;
    for(size_t i=0;i<512;++i) {
        struct kernel_task_handle h=dispatch(&d,&s,&r,&t);
        if(same(h,low)) seen_low=true;
        assert(kernel_task_yield(&t,h)==KERNEL_TASK_OK);
    }
    assert(seen_low);
    /* Tamper with a current generation profile: no task table mutation. */
    struct kernel_task_table original=t;
    struct d54_dispatch ages=d;
    s.profiles[high.slot].handle.generation++;
    struct kernel_task_handle out={UINT32_MAX,UINT32_MAX};
    assert(d54_dispatch_next(&d,&s,&r,&t,&out)!=KERNEL_TASK_OK);
    assert(memcmp(&t,&original,sizeof t)==0);
    assert(memcmp(&d,&ages,sizeof d)==0);
    assert(out.slot==UINT32_MAX);
}
int main(void)
{
    donation_dispatch();
    supervisor_budget();
    fairness_and_stale_profile();
    puts("SPIFFOS_M10_D54_PRIORITY_DISPATCH_OK");
    return 0;
}
