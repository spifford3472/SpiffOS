/* Purpose: hosted ASan/UBSan C1 test of lock-owning IRQ frame admission.
 * Responsibilities: prove genuine-looking complete frame geometry + D5.3
 * generation/counter graph before accepting a lock-bearing worker.
 * Dependencies: M10-A, D5.2b, D5.3, D5.8b, D5.8c.
 * Invariants: hosted frame/provenance is simulated, NOT a CPU IRQ;
 * rejects must never mutate any task, lock, resource or input frame.
 * Reference: ADR-m10-d58c-real-irq-lock-owner.md, AGENTS.md. */
#include "../src/kernel/m10-d58c-irq-frame.h"
#include "../src/kernel/m10-d2-irq-frame.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { TEST_BYTES=16384 };
static alignas(16) unsigned char owned_stack[TEST_BYTES];
static struct kernel_task_table tasks;
static struct kernel_task_resources resources;
static struct d53_sync locks;
static struct kernel_task_handle low;

/* Attach actual allocated test buffer as registered generation-owned stack,
 * not an address we invented without underlying accessible bytes. */
static uint64_t *setup(void)
{
    assert(kernel_task_init(&tasks) &&
           kernel_task_resources_init(&resources) &&
           d53_init(&locks));
    assert(kernel_task_create(&tasks,false,&low)==KERNEL_TASK_OK);
    const uintptr_t bottom=(uintptr_t)owned_stack;
    const struct kernel_task_stack_binding binding={
        .bottom=bottom,.bytes=TEST_BYTES,
        .saved_rsp=bottom+TEST_BYTES-48u,.owner_index=0};
    assert(kernel_task_resources_bind(&resources,&tasks,low,&binding)==
           KERNEL_RESOURCE_OK);
    assert(d53_enroll(&locks,&resources,&tasks,low,
                      KERNEL_PRIORITY_BACKGROUND)==D53_OK);
    assert(kernel_task_make_runnable(&tasks,low)==KERNEL_TASK_OK);
    struct kernel_task_handle current={0};
    assert(kernel_task_dispatch(&tasks,&current)==KERNEL_TASK_OK);
    assert(current.slot==low.slot && current.generation==low.generation);
    assert(d53_acquire(&locks,&resources,&tasks,0,low)==D53_OK);
    uint64_t *frame=(uint64_t *)(bottom+TEST_BYTES-512u);
    memset(frame,0,M10_D2_FRAME_QWORDS*sizeof(uint64_t));
    frame[M10_D2_RIP]=UINT64_C(0x0000000000100000);
    frame[M10_D2_CS]=8;
    frame[M10_D2_RFLAGS]=UINT64_C(0x202);
    frame[M10_D2_RSP]=bottom+TEST_BYTES-8u;
    frame[M10_D2_SS]=16;
    return frame;
}

/* Check one immutable admission with injected provenance; real QEMU test
 * separately verifies real IF and actual assembly-provided hardware frame. */
static bool check(const uint64_t *frame,bool irq,bool live_clear,bool guard_clear)
{
    return d58c_irq_frame_eligible(&tasks,&resources,&locks,low,
        KERNEL_TASK_RUNNING,frame,8,16,irq,live_clear,guard_clear);
}

/* Fully snapshot separate owners before any rejection to ensure neither
 * a frame, the M10-A table nor resource/D5.3 graph is repaired in place. */
static void reject_immutable(const uint64_t *frame,bool irq,bool live_clear,
                             bool guard_clear)
{
    const struct kernel_task_table before_tasks=tasks;
    const struct kernel_task_resources before_resources=resources;
    const struct d53_sync before_locks=locks;
    uint64_t frame_copy[M10_D2_FRAME_QWORDS]={0};
    if(frame==(const uint64_t *)(owned_stack+TEST_BYTES-512u))
        memcpy(frame_copy,frame,sizeof frame_copy);
    assert(!check(frame,irq,live_clear,guard_clear));
    assert(memcmp(&tasks,&before_tasks,sizeof tasks)==0);
    assert(memcmp(&resources,&before_resources,sizeof resources)==0);
    assert(memcmp(&locks,&before_locks,sizeof locks)==0);
    if(frame==(const uint64_t *)(owned_stack+TEST_BYTES-512u))
        assert(memcmp(frame_copy,frame,sizeof frame_copy)==0);
}

int main(void)
{
    uint64_t *frame=setup();
    assert(resources.task[low.slot].owned_locks==1);
    assert(check(frame,true,true,true));
    reject_immutable(frame,false,true,true);
    reject_immutable(frame,true,false,true);
    reject_immutable(frame,true,true,false);
    struct kernel_task_handle original=low;
    low.generation++;
    reject_immutable(frame,true,true,true);
    low=original;

    uint64_t original_cs=frame[M10_D2_CS];
    frame[M10_D2_CS]=0;
    reject_immutable(frame,true,true,true);
    frame[M10_D2_CS]=original_cs;
    uint64_t original_flags=frame[M10_D2_RFLAGS];
    frame[M10_D2_RFLAGS]=2;
    reject_immutable(frame,true,true,true);
    frame[M10_D2_RFLAGS]=original_flags;
    uint64_t original_rsp=frame[M10_D2_RSP];
    frame[M10_D2_RSP]=(uintptr_t)frame;
    reject_immutable(frame,true,true,true);
    frame[M10_D2_RSP]=original_rsp;
    reject_immutable((const uint64_t *)(uintptr_t)0x1000u,true,true,true);
    reject_immutable((const uint64_t *)((uintptr_t)frame+1u),true,true,true);

    ++resources.task[low.slot].owned_locks;
    reject_immutable(frame,true,true,true);
    --resources.task[low.slot].owned_locks;
    ++locks.locks[0].owner.generation;
    reject_immutable(frame,true,true,true);
    --locks.locks[0].owner.generation;
    assert(check(frame,true,true,true));
    assert(d53_release(&locks,&resources,&tasks,0,low)==D53_OK);
    assert(resources.task[low.slot].owned_locks==0);
    assert(check(frame,true,true,true));
    puts("SPIFFOS_M10_D58C_IRQ_FRAME_GRAPH_OK");
    return 0;
}
