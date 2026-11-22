/* Purpose: D5.8b Phase 1 hosted read-only frame-eligibility verification.
 * Responsibilities: prove legitimate lock holders survive graph admission;
 * reject blocked waiter, forged counters, stale generations and orphans.
 * Dependencies: M10-A, D5.2b resources, D5.3 graph and D5.8b predicate.
 * Invariants: tests metadata only, not real IRQ frame or stack switching;
 * every denial must leave authoritative input structures unchanged.
 * Reference: ADR-m10-d58b-frame-ownership-gate.md, AGENTS.md. */
#include "../src/kernel/m10-d58b-admission.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Synthetic disjoint owned stack addresses; never dereference them. */
static struct kernel_task_handle create(struct kernel_task_table *tasks,
    struct kernel_task_resources *resources,struct d53_sync *sync,
    size_t index,enum kernel_priority_class priority)
{
    struct kernel_task_handle result={0};
    assert(kernel_task_create(tasks,false,&result)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding stack={
        .bottom=(uintptr_t)0x100000u+index*0x10000u,
        .bytes=16384u,
        .saved_rsp=(uintptr_t)0x103fe0u+index*0x10000u,
        .owner_index=index
    };
    assert(kernel_task_resources_bind(resources,tasks,result,&stack)==
           KERNEL_RESOURCE_OK);
    assert(d53_enroll(sync,resources,tasks,result,priority)==D53_OK);
    assert(kernel_task_make_runnable(tasks,result)==KERNEL_TASK_OK);
    return result;
}

/* Caller supplies injected provenance. This check alone NEVER authorizes an
 * IRQ return; a separate bridge must authenticate actual CPU context. */
static bool eligible(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,const struct d53_sync *sync,
    struct kernel_task_handle task,enum kernel_task_state state)
{
    return d58b_lock_frame_eligible(tasks,resources,sync,task,state,
                                    true,true,true,true);
}

/* Refusal must not silently repair or mutate any D5.3/M10-A state. */
static void failed_immutable(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,const struct d53_sync *sync,
    struct kernel_task_handle task,enum kernel_task_state state)
{
    const struct kernel_task_table prior_tasks=*tasks;
    const struct kernel_task_resources prior_resources=*resources;
    const struct d53_sync prior_sync=*sync;
    assert(!eligible(tasks,resources,sync,task,state));
    assert(memcmp(tasks,&prior_tasks,sizeof *tasks)==0);
    assert(memcmp(resources,&prior_resources,sizeof *resources)==0);
    assert(memcmp(sync,&prior_sync,sizeof *sync)==0);
}

/* Real M10-A lifecycle transitions and D5.3 reference mutations are
 * intentionally driven OUTSIDE IRQ; eligibility itself is always read-only. */
static void prove_lock_owned_and_blocked(void)
{
    struct kernel_task_table tasks={0};
    struct kernel_task_resources resources={0};
    struct d53_sync sync={0};
    struct kernel_task_handle running={0};
    assert(kernel_task_init(&tasks));
    assert(kernel_task_resources_init(&resources));
    assert(d53_init(&sync));
    struct kernel_task_handle low=create(&tasks,&resources,&sync,0,
                                          KERNEL_PRIORITY_BACKGROUND);
    assert(kernel_task_dispatch(&tasks,&running)==KERNEL_TASK_OK);
    assert(running.slot==low.slot && eligible(&tasks,&resources,&sync,low,
                                             KERNEL_TASK_RUNNING));
    assert(d53_acquire(&sync,&resources,&tasks,0,low)==D53_OK);
    assert(resources.task[low.slot].owned_locks==1);
    assert(eligible(&tasks,&resources,&sync,low,KERNEL_TASK_RUNNING));
    assert(kernel_task_yield(&tasks,low)==KERNEL_TASK_OK);
    struct kernel_task_handle medium=create(&tasks,&resources,&sync,1,
                                             KERNEL_PRIORITY_NORMAL);
    struct kernel_task_handle high=create(&tasks,&resources,&sync,2,
                                           KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(kernel_task_dispatch_selected(&tasks,high,&running)==KERNEL_TASK_OK);
    assert(d53_acquire(&sync,&resources,&tasks,0,high)==D53_BLOCKED);
    assert(tasks.current_slot==SIZE_MAX && tasks.tasks[high.slot].state==
           KERNEL_TASK_BLOCKED && resources.task[high.slot].registered_waiters==1);
    assert(d58b_blocked_departure_metadata(&tasks,&resources,&sync,high,
                                           false,true));
    assert(!d58b_blocked_departure_metadata(&tasks,&resources,&sync,high,
                                            true,true));
    assert(!d58b_blocked_departure_metadata(&tasks,&resources,&sync,high,
                                            false,false));
    failed_immutable(&tasks,&resources,&sync,high,KERNEL_TASK_RUNNABLE);
    assert(kernel_task_dispatch_selected(&tasks,medium,&running)==KERNEL_TASK_OK);
    assert(eligible(&tasks,&resources,&sync,low,KERNEL_TASK_RUNNABLE));
    failed_immutable(&tasks,&resources,&sync,high,KERNEL_TASK_RUNNABLE);
    failed_immutable(&tasks,&resources,&sync,low,KERNEL_TASK_RUNNING);

    struct kernel_task_handle stale={low.slot,low.generation+1};
    failed_immutable(&tasks,&resources,&sync,stale,KERNEL_TASK_RUNNABLE);
    assert(!d58b_lock_frame_eligible(&tasks,&resources,&sync,low,
                                    KERNEL_TASK_RUNNABLE,false,true,true,true));
    assert(!d58b_lock_frame_eligible(&tasks,&resources,&sync,low,
                                    KERNEL_TASK_RUNNABLE,true,false,true,true));
    assert(!d58b_lock_frame_eligible(&tasks,&resources,&sync,low,
                                    KERNEL_TASK_RUNNABLE,true,true,false,true));
    assert(!d58b_lock_frame_eligible(&tasks,&resources,&sync,low,
                                    KERNEL_TASK_RUNNABLE,true,true,true,false));

    /* Even a valid owner must be rejected if any ledger/graph fact is
     * damaged. Restoring test snapshots does not mimic runtime recovery. */
    struct kernel_task_resources invalid_resources=resources;
    invalid_resources.task[low.slot].owned_locks++;
    failed_immutable(&tasks,&invalid_resources,&sync,low,KERNEL_TASK_RUNNABLE);
    invalid_resources=resources;
    invalid_resources.task[low.slot].handle.generation++;
    failed_immutable(&tasks,&invalid_resources,&sync,low,KERNEL_TASK_RUNNABLE);
    invalid_resources=resources;
    invalid_resources.task[low.slot].quarantined=true;
    failed_immutable(&tasks,&invalid_resources,&sync,low,KERNEL_TASK_RUNNABLE);
    invalid_resources=resources;
    invalid_resources.task[low.slot].stack.bottom=
        resources.task[medium.slot].stack.bottom;
    failed_immutable(&tasks,&invalid_resources,&sync,low,KERNEL_TASK_RUNNABLE);
    struct d53_sync wrong=sync;
    wrong.locks[0].owner.generation++;
    failed_immutable(&tasks,&resources,&wrong,low,KERNEL_TASK_RUNNABLE);
    wrong=sync;
    wrong.locks[0].waiters[0].generation++;
    failed_immutable(&tasks,&resources,&wrong,low,KERNEL_TASK_RUNNABLE);

    /* Legitimate atomic M10-A handoff after independent (hosted) policy
     * selection, followed by valid non-IRQ owner release and waiter wake. */
    struct kernel_task_handle chosen={0};
    assert(kernel_task_irq_handoff(&tasks,medium,low,&chosen)==KERNEL_TASK_OK);
    assert(eligible(&tasks,&resources,&sync,low,KERNEL_TASK_RUNNING));
    enum kernel_priority_class donated=KERNEL_PRIORITY_BACKGROUND;
    assert(d53_effective(&sync,&resources,&tasks,low,&donated)==D53_OK &&
           donated==KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(d53_release(&sync,&resources,&tasks,0,low)==D53_OK);
    assert(resources.task[low.slot].owned_locks==0);
    assert(resources.task[high.slot].owned_locks==1);
    assert(resources.task[high.slot].registered_waiters==0);
    assert(tasks.tasks[high.slot].state==KERNEL_TASK_RUNNABLE);
    assert(eligible(&tasks,&resources,&sync,high,KERNEL_TASK_RUNNABLE));
    assert(d53_effective(&sync,&resources,&tasks,low,&donated)==D53_OK &&
           donated==KERNEL_PRIORITY_BACKGROUND);

    /* Orphan the new owner while still READY; D5.3 preserves owner ref for
     * diagnosis but IRQ admission must reject every candidate globally. */
    struct kernel_task_table orphan_tasks=tasks;
    assert(kernel_task_terminate(&orphan_tasks,high)==KERNEL_TASK_OK);
    assert(d53_check(&sync,&resources,&orphan_tasks));
    assert(d53_orphan(&sync,&orphan_tasks,0)==D53_ORPHANED);
    failed_immutable(&orphan_tasks,&resources,&sync,low,KERNEL_TASK_RUNNING);
}
int main(void)
{
    prove_lock_owned_and_blocked();
    puts("SPIFFOS_M10_D58B_LOCK_FRAME_METADATA_OK");
    return 0;
}
