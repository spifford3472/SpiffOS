/* Purpose: D5.6 exact M10-A privileged IRQ handoff regression.
 * Responsibilities: confirm single RUNNING owner, exact generation,
 * immutable rejection and supervisor readiness under IRQ-like transitions.
 * Dependencies: M10-A kernel-task.c, no hardware IRQ or allocation.
 * Invariants: tests only; production IRQ caller still must authenticate
 * saved frames, IF and per-task lock/critical state before handoff.
 * Concurrency: one hosted thread, no simulated nested interrupts.
 * Reference: ADR-m10-d56-irq-handoff.md, AGENTS.md. */
#include "../src/kernel/kernel-task.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void swap(struct kernel_task_table *t,
    struct kernel_task_handle from,struct kernel_task_handle to)
{
    struct kernel_task_handle got={0};
    assert(kernel_task_irq_handoff(t,from,to,&got)==KERNEL_TASK_OK);
    assert(got.slot==to.slot && got.generation==to.generation);
    assert(kernel_task_check(t));
    assert(t->current_slot==to.slot);
}

/* Every invalid identity, state and output must fail without altering the
 * existing authoritative task table; no speculative partial yield. */
static void test_handoff(void)
{
    struct kernel_task_table t={0};
    assert(kernel_task_init(&t));
    struct kernel_task_handle a={0},b={0},sup={0},got={0};
    assert(kernel_task_create(&t,false,&a)==KERNEL_TASK_OK);
    assert(kernel_task_create(&t,false,&b)==KERNEL_TASK_OK);
    assert(kernel_task_create(&t,true,&sup)==KERNEL_TASK_OK);
    assert(sup.slot==KERNEL_TASK_SUPERVISOR_SLOT);
    assert(kernel_task_make_runnable(&t,a)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&t,b)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&t,sup)==KERNEL_TASK_OK);
    assert(kernel_task_dispatch(&t,&got)==KERNEL_TASK_OK);
    assert(got.slot==a.slot);
    struct kernel_task_table original=t;
    struct kernel_task_handle unchanged={UINT32_MAX,UINT32_MAX};
    struct kernel_task_handle stale=b;
    ++stale.generation;
    assert(kernel_task_irq_handoff(&t,a,stale,&unchanged)==
           KERNEL_TASK_BAD_HANDLE);
    assert(memcmp(&original,&t,sizeof t)==0);
    assert(unchanged.slot==UINT32_MAX);
    stale=a;++stale.generation;
    assert(kernel_task_irq_handoff(&t,stale,b,&unchanged)==
           KERNEL_TASK_BAD_HANDLE);
    assert(memcmp(&original,&t,sizeof t)==0);
    assert(kernel_task_irq_handoff(&t,b,a,&unchanged)==
           KERNEL_TASK_BAD_STATE);
    assert(memcmp(&original,&t,sizeof t)==0);
    assert(kernel_task_irq_handoff(&t,a,b,NULL)==KERNEL_TASK_INVALID);
    assert(memcmp(&original,&t,sizeof t)==0);
    swap(&t,a,b);
    swap(&t,b,sup);
    swap(&t,sup,a);
    assert(kernel_task_irq_handoff(&t,a,a,&got)==KERNEL_TASK_OK);
    assert(got.slot==a.slot);
    for(unsigned k=0;k<20000;++k) {
        swap(&t,a,b);
        swap(&t,b,sup);
        swap(&t,sup,a);
    }
    assert(t.tasks[a.slot].dispatches>10000);
    assert(t.tasks[b.slot].dispatches>10000);
    assert(t.tasks[sup.slot].dispatches>10000);
    puts("SPIFFOS_M10_D56_IRQ_HANDOFF_OK");
}
int main(void){test_handoff();return 0;}
