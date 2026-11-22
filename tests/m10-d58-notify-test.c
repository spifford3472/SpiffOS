/* Purpose: hosted D5.8a diagnostic notification invariants and negatives.
 * Responsibilities: verify exact identity, strict provenance, bounded FIFO,
 * no task-table mutation, stale refusal, overflow and permanent fault.
 * Dependencies: M10-A and D5.8a queue, GCC ASan/UBSan.
 * Invariants: hosted flags are injected witnesses, not real CPU state.
 * Reference: ADR-m10-d58-irq-mutex-integration.md, AGENTS.md. */
#include "../src/kernel/m10-d58-notify.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Set up one real authoritative RUNNING generation with no IRQ handling. */
static struct kernel_task_handle running(struct kernel_task_table *t)
{
    struct kernel_task_handle h={0},selected={0};
    assert(kernel_task_init(t));
    assert(kernel_task_create(t,false,&h)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(t,h)==KERNEL_TASK_OK);
    assert(kernel_task_dispatch(t,&selected)==KERNEL_TASK_OK);
    assert(selected.slot==h.slot && selected.generation==h.generation);
    return h;
}

/* A failed admission may never accidentally enqueue or mutate M10-A. */
static void invalid_source(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_handle h=running(&t);
    const struct kernel_task_table original=t;
    struct d58_notice_queue q={0};
    assert(d58_queue_init(&q));
    assert(!d58_note_irq(&q,&t,h,1,false,true));
    assert(q.faulted && q.count==0);
    assert(!d58_queue_init(&q));
    assert(!d58_note_irq(&q,&t,h,2,true,true));
    assert(memcmp(&t,&original,sizeof t)==0);
    struct d58_notice_queue stale={0};
    assert(d58_queue_init(&stale));
    struct kernel_task_handle forged={h.slot,h.generation+1};
    assert(!d58_note_irq(&stale,&t,forged,3,true,true));
    assert(stale.faulted && stale.count==0);
    struct d58_notice_queue if_on={0};
    assert(d58_queue_init(&if_on));
    assert(!d58_note_irq(&if_on,&t,h,1,true,false));
    assert(if_on.faulted && if_on.count==0);
}

/* Bounded queue wraps safely and yields snapshots only from supervisor. */
static void fifo_and_overflow(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_handle h=running(&t);
    const struct kernel_task_table original=t;
    struct d58_notice_queue q={0};
    assert(d58_queue_init(&q));
    for(unsigned i=0;i<D58_NOTICE_CAPACITY;++i)
        assert(d58_note_irq(&q,&t,h,i+1,true,true));
    struct d58_notice out={{99,99},99};
    assert(!d58_take_supervisor(&q,&t,&out,true,true,true));
    assert(q.faulted && q.count==D58_NOTICE_CAPACITY);
    assert(out.tick==99 && memcmp(&t,&original,sizeof t)==0);
    struct d58_notice_queue full={0};
    assert(d58_queue_init(&full));
    for(unsigned i=0;i<D58_NOTICE_CAPACITY;++i)
        assert(d58_note_irq(&full,&t,h,i+1,true,true));
    assert(!d58_note_irq(&full,&t,h,10,true,true));
    assert(full.faulted && full.count==D58_NOTICE_CAPACITY);
    struct d58_notice_queue ok={0};
    assert(d58_queue_init(&ok));
    for(unsigned i=0;i<D58_NOTICE_CAPACITY;++i)
        assert(d58_note_irq(&ok,&t,h,i+1,true,true));
    for(unsigned i=0;i<3;++i) {
        assert(d58_take_supervisor(&ok,&t,&out,false,true,true));
        assert(out.tick==i+1 && out.observed.generation==h.generation);
    }
    for(unsigned i=0;i<3;++i)
        assert(d58_note_irq(&ok,&t,h,D58_NOTICE_CAPACITY+i+1,true,true));
    for(unsigned i=3;i<D58_NOTICE_CAPACITY+3;++i) {
        assert(d58_take_supervisor(&ok,&t,&out,false,true,true));
        assert(out.tick==i+1);
    }
    assert(ok.count==0 && !ok.faulted);
    /* A drained queue must still reject a rolled-back monotonic tick. */
    assert(!d58_note_irq(&ok,&t,h,1,true,true));
    assert(ok.faulted && ok.count==0);
    assert(!d58_take_supervisor(&ok,&t,&out,false,true,true));
    assert(ok.faulted && memcmp(&t,&original,sizeof t)==0);
}

/* A stale generation after legitimate termination must not be consumed. */
static void stale_and_churn(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_handle h=running(&t);
    struct d58_notice_queue q={0};
    assert(d58_queue_init(&q));
    assert(d58_note_irq(&q,&t,h,1,true,true));
    assert(kernel_task_terminate(&t,h)==KERNEL_TASK_OK);
    const struct kernel_task_table original=t;
    struct d58_notice out={{88,88},88};
    assert(!d58_take_supervisor(&q,&t,&out,false,true,true));
    assert(q.faulted && q.count==1 && out.tick==88);
    assert(memcmp(&t,&original,sizeof t)==0);
}

int main(void)
{
    invalid_source();
    fifo_and_overflow();
    stale_and_churn();
    puts("SPIFFOS_M10_D58_DEFERRED_NOTIFICATION_OK");
    return 0;
}
