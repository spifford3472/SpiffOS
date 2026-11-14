/* Purpose: D5.5c sanitizer validation for exact IRQ frame ownership.
 * Responsibilities: validate owner/generation, synthetic frame geometry,
 * IRQ/caller prerequisites, lock interlock, monotonic tick service timing.
 * Dependencies: M10-A, D5.2b resource registry and D5.5c policy.
 * Invariants: no runtime CPU claim; synthetic frames only in hosted tests.
 * Concurrency: single host thread with no actual IRQ handler.
 * References: ADR-m10-d55c-task-frame-budget.md and AGENTS.md. */
#include "../src/kernel/m10-d55c-frame-owner.h"
#include "../src/kernel/m10-d2-irq-frame.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t simulated_stack[2048];
static struct kernel_task_handle setup(struct kernel_task_table *tasks,
    struct kernel_task_resources *resources, uint64_t **frame)
{
    assert(kernel_task_init(tasks) && kernel_task_resources_init(resources));
    struct kernel_task_handle handle={0};
    assert(kernel_task_create(tasks,false,&handle)==KERNEL_TASK_OK);
    uintptr_t base=(uintptr_t)simulated_stack;
    uintptr_t top=base+sizeof simulated_stack;
    uint64_t *words=(uint64_t *)(top-40u*sizeof(uint64_t));
    for(size_t i=0;i<M10_D2_FRAME_QWORDS;++i) words[i]=0;
    words[M10_D2_RIP]=UINT64_C(0x0000000000100000);
    words[M10_D2_RSP]=top-8u;
    words[M10_D2_CS]=UINT64_C(0x08);
    words[M10_D2_SS]=UINT64_C(0x10);
    words[M10_D2_RFLAGS]=UINT64_C(0x202);
    struct kernel_task_stack_binding binding={
        .bottom=base,.bytes=sizeof simulated_stack,
        .saved_rsp=(uintptr_t)words,
        .owner_index=0
    };
    assert(kernel_task_resources_bind(resources,tasks,handle,&binding)==KERNEL_RESOURCE_OK);
    assert(kernel_task_make_runnable(tasks,handle)==KERNEL_TASK_OK);
    struct kernel_task_handle selected={0};
    assert(kernel_task_dispatch(tasks,&selected)==KERNEL_TASK_OK);
    assert(selected.slot==handle.slot &&
           selected.generation==handle.generation);
    *frame=words;
    return handle;
}

/* Preflight must reject hostile evidence before dereferencing it; exact
 * handle and known 160-byte saved frame must be within owned stack. */
static void test_binding(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    uint64_t *frame=NULL;
    struct kernel_task_handle h=setup(&t,&r,&frame);
    struct d55c_irq_snapshot e={
        .current=h,.saved_frame=frame,
        .expected_cs=0x08,.expected_ss=0x10,
        .cpu_if_clear=true,.irq_depth=1
    };
    struct kernel_task_table t_saved=t;
    struct kernel_task_resources r_saved=r;
    assert(d55c_frame_matches(&t,&r,&e));
    ++e.current.generation;
    assert(!d55c_frame_matches(&t,&r,&e));
    e.current=h;
    e.saved_frame=(const uint64_t *)(uintptr_t)0x1230;
    assert(!d55c_frame_matches(&t,&r,&e));
    e.saved_frame=frame;e.irq_depth=2;
    assert(!d55c_frame_matches(&t,&r,&e));
    e.irq_depth=1;e.critical_depth=1;
    assert(!d55c_frame_matches(&t,&r,&e));
    e.critical_depth=0;e.cpu_if_clear=false;
    assert(!d55c_frame_matches(&t,&r,&e));
    e.cpu_if_clear=true;e.expected_cs=0;
    assert(!d55c_frame_matches(&t,&r,&e));
    e.expected_cs=0x08;
    frame[M10_D2_RIP]=UINT64_C(0x0001000000000000);
    assert(!d55c_frame_matches(&t,&r,&e));
    frame[M10_D2_RIP]=UINT64_C(0x100000);
    frame[M10_D2_RFLAGS]=0x2;
    assert(!d55c_frame_matches(&t,&r,&e));
    frame[M10_D2_RFLAGS]=0x202;
    frame[M10_D2_RSP]=r.task[h.slot].stack.bottom-1;
    assert(!d55c_frame_matches(&t,&r,&e));
    frame[M10_D2_RSP]=r.task[h.slot].stack.bottom+sizeof simulated_stack-8;
    assert(d55c_frame_matches(&t,&r,&e));
    assert(kernel_task_resources_ref(&r,&t,h,KERNEL_RESOURCE_LOCK,true)==
           KERNEL_RESOURCE_OK);
    assert(!d55c_frame_matches(&t,&r,&e));
    assert(kernel_task_resources_ref(&r,&t,h,KERNEL_RESOURCE_LOCK,false)==
           KERNEL_RESOURCE_OK);
    assert(d55c_frame_matches(&t,&r,&e));
    assert(memcmp(&t,&t_saved,sizeof t)==0);
    assert(memcmp(&r,&r_saved,sizeof r)==0);
}

/* On a non-yielding worker, IRQ tick progression cannot be equated with a
 * supervisor dispatch attempt; *only* verified service resets the deadline. */
static void test_tick_service(void)
{
    struct d55c_tick_service m={0};
    assert(!d55c_tick_init(&m,100,0));
    assert(d55c_tick_init(&m,100,4));
    assert(d55c_tick_observe(&m,true,104));
    assert(m.max_observed_gap_ticks==4 && m.service_completions==0);
    assert(!d55c_tick_service_completed(&m,false,true,104));
    assert(m.service_completions==0);
    assert(!d55c_tick_observe(&m,true,105) && m.faulted);
    assert(!d55c_tick_service_completed(&m,true,true,105));
    struct d55c_tick_service healthy={0};
    assert(d55c_tick_init(&healthy,0,4));
    for(uint64_t i=1;i<=20000;++i) {
        assert(d55c_tick_observe(&healthy,true,i));
        assert(d55c_tick_service_completed(&healthy,true,true,i));
    }
    assert(healthy.service_completions==20000 &&
           healthy.max_observed_gap_ticks==1 && !healthy.faulted);
    assert(!d55c_tick_observe(&healthy,true,0) && healthy.faulted);
    struct d55c_tick_service missing={0};
    assert(d55c_tick_init(&missing,100,4));
    assert(!d55c_tick_observe(&missing,false,101) && missing.faulted);
    struct d55c_tick_service overflow={0};
    assert(d55c_tick_init(&overflow,UINT64_MAX-2,2));
    assert(d55c_tick_observe(&overflow,true,UINT64_MAX));
    assert(!d55c_tick_observe(&overflow,true,0) && overflow.faulted);
}

/* Reject a real table that is running a different generation/slot. */
static void test_wrong_owner(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    uint64_t *frame=NULL;
    struct kernel_task_handle h=setup(&t,&r,&frame);
    struct d55c_irq_snapshot e={.current=h,.saved_frame=frame,
        .expected_cs=8,.expected_ss=16,.cpu_if_clear=true,.irq_depth=1};
    assert(kernel_task_yield(&t,h)==KERNEL_TASK_OK);
    assert(!d55c_frame_matches(&t,&r,&e));
}
int main(void)
{
    test_binding();
    test_tick_service();
    test_wrong_owner();
    puts("SPIFFOS_M10_D55C_FRAME_TICK_OK");
    return 0;
}
