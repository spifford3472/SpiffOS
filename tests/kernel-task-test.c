/* Purpose: M10-A pure scheduler hosted regression and negative tests.
 * Dependencies: kernel-task.h, assertions and host compiler only.
 * Invariants: every mutation passes kernel_task_check; rejection preserves
 * state and outputs; no hardware timer or context switch is used.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "../src/kernel/kernel-task.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Validate that a refused operation did not modify any caller-owned state. */
static void unchanged(const struct kernel_task_table *table,
                      const struct kernel_task_table *before)
{
    assert(memcmp(table,before,sizeof *table) == 0);
    assert(kernel_task_check(table));
}

/* Verify empty queue, FIFO fairness, current-owner restrictions and block/wake. */
static void lifecycle(void)
{
    struct kernel_task_table table = {0};
    assert(kernel_task_init(&table));
    assert(!kernel_task_init(&table));
    assert(kernel_task_check(&table));
    struct kernel_task_handle out = {123,456};
    assert(kernel_task_dispatch(&table,&out)==KERNEL_TASK_NO_RUNNABLE);
    assert(out.slot==123 && out.generation==456);
    struct kernel_task_handle a,b,c;
    assert(kernel_task_create(&table,false,&a)==KERNEL_TASK_OK);
    assert(kernel_task_create(&table,false,&b)==KERNEL_TASK_OK);
    assert(kernel_task_create(&table,false,&c)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&table,a)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&table,b)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&table,c)==KERNEL_TASK_OK);
    struct kernel_task_handle current;
    for (unsigned i=0; i<24; ++i) {
        assert(kernel_task_dispatch(&table,&current)==KERNEL_TASK_OK);
        assert(current.slot==(i%3));
        assert(kernel_task_dispatch(&table,&out)==KERNEL_TASK_BAD_STATE);
        assert(kernel_task_yield(&table,current)==KERNEL_TASK_OK);
        assert(kernel_task_check(&table));
    }
    assert(kernel_task_dispatch(&table,&current)==KERNEL_TASK_OK);
    assert(current.slot==a.slot);
    assert(kernel_task_block(&table,current)==KERNEL_TASK_OK);
    assert(kernel_task_wake(&table,a)==KERNEL_TASK_OK);
    assert(kernel_task_dispatch(&table,&current)==KERNEL_TASK_OK);
    assert(current.slot==b.slot);
    assert(kernel_task_terminate(&table,current)==KERNEL_TASK_OK);
    assert(kernel_task_check(&table));
    assert(kernel_task_terminate(&table,c)==KERNEL_TASK_OK);
    assert(kernel_task_dispatch(&table,&current)==KERNEL_TASK_OK);
    assert(current.slot==a.slot);
    assert(kernel_task_block(&table,current)==KERNEL_TASK_OK);
    assert(kernel_task_dispatch(&table,&current)==KERNEL_TASK_NO_RUNNABLE);
    assert(kernel_task_terminate(&table,a)==KERNEL_TASK_OK);
    assert(kernel_task_check(&table));
}

/* Verify supervisor reserve, generation reuse and rejection atomicity. */
static void exhaustion(void)
{
    struct kernel_task_table table = {0};
    assert(kernel_task_init(&table));
    struct kernel_task_handle normal[KERNEL_TASK_SUPERVISOR_SLOT],supervisor;
    for(size_t i=0;i<KERNEL_TASK_SUPERVISOR_SLOT;++i)
        assert(kernel_task_create(&table,false,&normal[i])==KERNEL_TASK_OK);
    struct kernel_task_handle sentinel={77,99};
    struct kernel_task_table before=table;
    assert(kernel_task_create(&table,false,&sentinel)==KERNEL_TASK_NO_CAPACITY);
    unchanged(&table,&before);
    assert(sentinel.slot==77 && sentinel.generation==99);
    assert(kernel_task_create(&table,true,&supervisor)==KERNEL_TASK_OK);
    assert(supervisor.slot==KERNEL_TASK_SUPERVISOR_SLOT);
    before=table;
    assert(kernel_task_create(&table,true,&sentinel)==KERNEL_TASK_NO_CAPACITY);
    unchanged(&table,&before);
    assert(kernel_task_terminate(&table,normal[0])==KERNEL_TASK_OK);
    struct kernel_task_handle replacement;
    assert(kernel_task_create(&table,false,&replacement)==KERNEL_TASK_OK);
    assert(replacement.slot==normal[0].slot);
    assert(replacement.generation!=normal[0].generation);
    before=table;
    assert(kernel_task_make_runnable(&table,normal[0])==KERNEL_TASK_BAD_HANDLE);
    unchanged(&table,&before);
    assert(kernel_task_get(&table,normal[0])==NULL);
    assert(kernel_task_get(&table,replacement)!=NULL);
}

/* Check all documented illegal state transitions and queue corruption. */
static void invalid_and_corrupt(void)
{
    struct kernel_task_table table={0};
    assert(kernel_task_init(&table));
    struct kernel_task_handle a;
    assert(kernel_task_create(&table,false,&a)==KERNEL_TASK_OK);
    struct kernel_task_table before=table;
    assert(kernel_task_wake(&table,a)==KERNEL_TASK_BAD_STATE);
    unchanged(&table,&before);
    assert(kernel_task_yield(&table,a)==KERNEL_TASK_BAD_STATE);
    unchanged(&table,&before);
    assert(kernel_task_make_runnable(&table,a)==KERNEL_TASK_OK);
    before=table;
    assert(kernel_task_make_runnable(&table,a)==KERNEL_TASK_BAD_STATE);
    unchanged(&table,&before);
    assert(kernel_task_block(&table,a)==KERNEL_TASK_BAD_STATE);
    unchanged(&table,&before);
    struct kernel_task_handle running;
    assert(kernel_task_dispatch(&table,&running)==KERNEL_TASK_OK);
    before=table;
    assert(kernel_task_wake(&table,a)==KERNEL_TASK_BAD_STATE);
    unchanged(&table,&before);
    assert(kernel_task_terminate(&table,a)==KERNEL_TASK_OK);
    before=table;
    assert(kernel_task_terminate(&table,a)==KERNEL_TASK_BAD_STATE);
    unchanged(&table,&before);
    table.ready_queue[0]=KERNEL_TASK_CAPACITY;
    table.ready_count=1;
    assert(!kernel_task_check(&table));
    before=table;
    assert(kernel_task_create(&table,false,&running)==KERNEL_TASK_CORRUPT);
    assert(memcmp(&table,&before,sizeof table)==0);
}

/* A terminal generation prevents stale handle aliasing after 2^32 cycles. */
static void generation_boundary(void)
{
    struct kernel_task_table table={0};
    assert(kernel_task_init(&table));
    for(size_t i=0;i<KERNEL_TASK_SUPERVISOR_SLOT;++i) {
        table.tasks[i].state=KERNEL_TASK_TERMINATED;
        table.tasks[i].generation=UINT32_MAX;
    }
    assert(kernel_task_check(&table));
    struct kernel_task_handle sentinel={99,100};
    struct kernel_task_table before=table;
    assert(kernel_task_create(&table,false,&sentinel)==
           KERNEL_TASK_GENERATION_EXHAUSTED);
    unchanged(&table,&before);
    assert(sentinel.slot==99 && sentinel.generation==100);
}

int main(void)
{
    lifecycle();
    exhaustion();
    invalid_and_corrupt();
    generation_boundary();
    puts("SPIFFOS_M10_A_POLICY_OK");
    return 0;
}
