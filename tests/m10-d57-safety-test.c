/* Purpose: M10-D5.7 hosted sanitizer preempt/priority acceptance.
 * Responsibilities: verify exact-generation LIFO exclusion, permanent
 * guard faults, donated lock priority and M10-A's only state mutation.
 * Dependencies: M10-A, D5.2b resource bindings, D5.3 lock profiles,
 * D5.7 read-only IRQ selector and D1 priority policy.
 * Invariants: no actual IRQ or CPU frame in hosted tests; never claim
 * production driver integration, release ownership or realtime guarantee.
 * Reference: ADR-m10-d57-preemption-and-priority.md, AGENTS.md. */
#include "../src/kernel/m10-d57-safety.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* An exact handle's nested lock guard cannot be popped out-of-order,
 * stolen by a recycled generation, or used in IRQ. Any violation latches. */
static void guard_cases(void)
{
    struct d57_preempt_guard g={0};
    struct kernel_task_handle task={1,2},stale={1,3};
    uint32_t first=0,second=0;
    assert(d57_guard_init(&g));
    assert(d57_guard_enter(&g,task,false,true,&first));
    assert(d57_guard_enter(&g,task,false,true,&second));
    assert(first!=second && g.depth==2);
    assert(!d57_guard_may_switch(&g,true,true,true));
    assert(d57_guard_exit(&g,task,false,true,second));
    assert(d57_guard_exit(&g,task,false,true,first));
    assert(g.depth==0 && d57_guard_may_switch(&g,true,true,true));
    assert(!d57_guard_may_switch(&g,true,false,true));
    assert(!d57_guard_may_switch(&g,false,true,true));
    assert(!d57_guard_may_switch(&g,true,true,false));
    assert(d57_guard_enter(&g,task,false,true,&first));
    assert(!d57_guard_exit(&g,stale,false,true,first));
    assert(g.faulted && !d57_guard_may_switch(&g,true,true,true));
    assert(!d57_guard_init(&g));
    assert(!d57_guard_exit(&g,task,false,true,first));
    struct d57_preempt_guard overflow={0};
    assert(d57_guard_init(&overflow));
    uint32_t cookies[D57_MAX_GUARD_DEPTH]={0};
    for(unsigned i=0;i<D57_MAX_GUARD_DEPTH;++i)
        assert(d57_guard_enter(&overflow,task,false,true,&cookies[i]));
    assert(!d57_guard_enter(&overflow,task,false,true,&first));
    assert(overflow.faulted);
    struct d57_preempt_guard wrong_order={0};
    assert(d57_guard_init(&wrong_order));
    assert(d57_guard_enter(&wrong_order,task,false,true,&first));
    assert(d57_guard_enter(&wrong_order,task,false,true,&second));
    assert(!d57_guard_exit(&wrong_order,task,false,true,first));
    assert(wrong_order.faulted);
    struct d57_preempt_guard irq={0};
    assert(d57_guard_init(&irq));
    assert(!d57_guard_enter(&irq,task,true,true,&first));
    assert(irq.faulted);
    struct d57_preempt_guard disabled={0};
    assert(d57_guard_init(&disabled));
    assert(!d57_guard_enter(&disabled,task,false,false,&first));
    assert(disabled.faulted);
}

/* Attach unique synthetic 16KiB binding for D5.3 exact identity. */
static struct kernel_task_handle create(
    struct kernel_task_table *tasks,
    struct kernel_task_resources *resources,
    struct d53_sync *sync,size_t index,
    enum kernel_priority_class priority)
{
    struct kernel_task_handle h={0};
    assert(kernel_task_create(tasks,false,&h)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding stack={
        .bottom=(uintptr_t)0x100000u+index*0x10000u,
        .bytes=16384,.saved_rsp=(uintptr_t)0x103fe0u+index*0x10000u,
        .owner_index=index
    };
    assert(kernel_task_resources_bind(resources,tasks,h,&stack)==KERNEL_RESOURCE_OK);
    assert(d53_enroll(sync,resources,tasks,h,priority)==D53_OK);
    assert(kernel_task_make_runnable(tasks,h)==KERNEL_TASK_OK);
    return h;
}

/* The truly running medium task is preempted by donated LOW owner while
 * high waiter BLOCKED. D5.3 acquisition ran outside IRQ; selection is
 * entirely read-only until M10-A's exact privileged handoff commits. */
static void donated_irq_selection(void)
{
    struct kernel_task_table tasks={0};
    struct kernel_task_resources resources={0};
    struct d53_sync sync={0};
    struct d57_irq_policy policy={0};
    struct kernel_task_handle running={0},chosen={0};
    assert(kernel_task_init(&tasks) &&
           kernel_task_resources_init(&resources) &&
           d53_init(&sync) && d57_policy_init(&policy));
    struct kernel_task_handle low=create(
        &tasks,&resources,&sync,0,KERNEL_PRIORITY_BACKGROUND);
    assert(kernel_task_dispatch(&tasks,&running)==KERNEL_TASK_OK);
    assert(running.slot==low.slot);
    assert(d53_acquire(&sync,&resources,&tasks,0,low)==D53_OK);
    assert(kernel_task_yield(&tasks,low)==KERNEL_TASK_OK);
    struct kernel_task_handle medium=create(
        &tasks,&resources,&sync,1,KERNEL_PRIORITY_NORMAL);
    struct kernel_task_handle high=create(
        &tasks,&resources,&sync,2,KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(kernel_task_dispatch_selected(&tasks,high,&running)==KERNEL_TASK_OK);
    assert(d53_acquire(&sync,&resources,&tasks,0,high)==D53_BLOCKED);
    assert(kernel_task_dispatch_selected(&tasks,medium,&running)==KERNEL_TASK_OK);
    enum kernel_priority_class eff=KERNEL_PRIORITY_BACKGROUND;
    assert(d53_effective(&sync,&resources,&tasks,low,&eff)==D53_OK);
    assert(eff==KERNEL_PRIORITY_TIME_SENSITIVE);
    struct kernel_task_table pristine_tasks=tasks;
    struct kernel_task_resources pristine_resources=resources;
    struct d53_sync pristine_sync=sync;
    struct d57_irq_policy pristine_policy=policy;
    assert(d57_select(&policy,&sync,&resources,&tasks,&chosen));
    assert(chosen.slot==low.slot && chosen.generation==low.generation);
    assert(memcmp(&tasks,&pristine_tasks,sizeof tasks)==0);
    assert(memcmp(&resources,&pristine_resources,sizeof resources)==0);
    assert(memcmp(&sync,&pristine_sync,sizeof sync)==0);
    assert(memcmp(&policy,&pristine_policy,sizeof policy)==0);
    struct kernel_task_handle switched={0};
    assert(kernel_task_irq_handoff(&tasks,medium,chosen,&switched)==KERNEL_TASK_OK);
    assert(switched.slot==low.slot);
    assert(d57_note_handoff(&policy,&tasks,switched));
    assert(tasks.current_slot==low.slot);
    /* After normal safe-point release, the effective donation is
     * withdrawn. High is woken to RUNNABLE in M10-A, not invented. */
    assert(d53_release(&sync,&resources,&tasks,0,low)==D53_OK);
    assert(d53_effective(&sync,&resources,&tasks,low,&eff)==D53_OK);
    assert(eff==KERNEL_PRIORITY_BACKGROUND);
    assert(d57_select(&policy,&sync,&resources,&tasks,&chosen));
    assert(chosen.slot==high.slot);
    /* Validate immutable fail-closed policy on stale profile. */
    struct d57_irq_policy snapshot=policy;
    struct kernel_task_table snapshot_tasks=tasks;
    sync.profiles[high.slot].handle.generation++;
    struct kernel_task_handle no_output={UINT32_MAX,UINT32_MAX};
    assert(!d57_select(&policy,&sync,&resources,&tasks,&no_output));
    assert(no_output.slot==UINT32_MAX);
    assert(memcmp(&snapshot,&policy,sizeof policy)==0);
    assert(memcmp(&snapshot_tasks,&tasks,sizeof tasks)==0);
}

/* Every fourth policy decision reserves a runnable supervisor, but
 * it remains only dispatch eligibility, not completed CPU-time service. */
static void supervisor_fairness(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct d53_sync s={0};
    struct d57_irq_policy p={0};
    assert(kernel_task_init(&t) && kernel_task_resources_init(&r) &&
           d53_init(&s) && d57_policy_init(&p));
    struct kernel_task_handle workers[2];
    workers[0]=create(&t,&r,&s,0,KERNEL_PRIORITY_TIME_SENSITIVE);
    workers[1]=create(&t,&r,&s,1,KERNEL_PRIORITY_BACKGROUND);
    struct kernel_task_handle supervisor={0};
    assert(kernel_task_create(&t,true,&supervisor)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&t,supervisor)==KERNEL_TASK_OK);
    struct kernel_task_handle running={0};
    assert(kernel_task_dispatch(&t,&running)==KERNEL_TASK_OK);
    size_t gap=0,supervisor_runs=0,background_runs=0;
    for(size_t i=0;i<20000;++i) {
        struct kernel_task_handle target={0};
        assert(d57_select(&p,&s,&r,&t,&target));
        struct kernel_task_handle actual={0};
        assert(kernel_task_irq_handoff(&t,running,target,&actual)==KERNEL_TASK_OK);
        assert(d57_note_handoff(&p,&t,actual));
        running=actual;
        assert(kernel_task_check(&t));
        if(actual.slot==supervisor.slot) {
            assert(gap<=4);gap=0;++supervisor_runs;
        }else ++gap;
        if(actual.slot==workers[1].slot) ++background_runs;
    }
    assert(supervisor_runs>=4000 && background_runs>0);
}
int main(void)
{
    guard_cases();
    donated_irq_selection();
    supervisor_fairness();
    puts("SPIFFOS_M10_D57_GUARD_DONATION_OK");
    return 0;
}
