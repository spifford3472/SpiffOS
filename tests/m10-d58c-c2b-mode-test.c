/* Purpose: hosted C2b Phase 0 exact-generation RET/IRETQ IRQ admission.
 * Responsibilities: create real M10-A/D5.3 low-owner/high BLOCKED/medium
 * READY graph and prove a RET waiter NEVER becomes an IRQ target on wake.
 * Dependencies: D5.2b registry, D5.3, C1, C2b read-only resume gate.
 * Invariants: simulated frames are not real CPU provenance; all refusal
 * paths leave M10-A, locks, ledgers and original frame bytes unchanged.
 * Concurrency: hosted single-threaded C11; no real interrupts or switch.
 * References: ADR-m10-d58c-c2b-context-modes.md, AGENTS.md. */
#include "../src/kernel/m10-d58c-c2b-resume-gate.h"
#include "../src/kernel/m10-d2-irq-frame.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { FRAME_STACK_BYTES=16384, FRAME_OFFSET=512 };
static alignas(16) unsigned char backing[3][FRAME_STACK_BYTES];
static struct kernel_task_table tasks;
static struct kernel_task_resources resources;
static struct d53_sync sync;
static struct kernel_task_handle low,high,medium;

/* Three independent actual host stack buffers serve as allocated bound
 * backing for simulated CPU frame bytes; they are never executed as stacks. */
static struct kernel_task_handle new_task(size_t index,
    enum kernel_priority_class priority)
{
    struct kernel_task_handle h={0};
    assert(kernel_task_create(&tasks,false,&h)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding binding={
        .bottom=(uintptr_t)backing[index],
        .bytes=FRAME_STACK_BYTES,
        .saved_rsp=(uintptr_t)backing[index]+FRAME_STACK_BYTES-48u,
        .owner_index=index
    };
    assert(kernel_task_resources_bind(&resources,&tasks,h,&binding)==
           KERNEL_RESOURCE_OK);
    assert(d53_enroll(&sync,&resources,&tasks,h,priority)==D53_OK);
    assert(kernel_task_make_runnable(&tasks,h)==KERNEL_TASK_OK);
    return h;
}

/* Allocate no frame: place an initial synthetic test image entirely within
 * each independent bound buffer. The later real bridge MUST authenticate
 * a genuinely hardware-supplied or trusted-init source of the target image. */
static uint64_t *synthetic_frame(size_t index)
{
    const uintptr_t bottom=(uintptr_t)backing[index];
    uint64_t *frame=(uint64_t *)(bottom+FRAME_STACK_BYTES-FRAME_OFFSET);
    memset(frame,0,M10_D2_FRAME_QWORDS*sizeof(uint64_t));
    frame[M10_D2_RIP]=UINT64_C(0x100000);
    frame[M10_D2_CS]=8;
    frame[M10_D2_RFLAGS]=UINT64_C(0x202);
    frame[M10_D2_RSP]=bottom+FRAME_STACK_BYTES-8u;
    frame[M10_D2_SS]=16;
    return frame;
}

/* Read-only checks may not mutate any M10-A/ledger/lock/frame state; the
 * saved frame is a simulated C1 complete-frame proof, not an IRQ record. */
static bool gate(struct kernel_task_handle src,const uint64_t *source_frame,
                 const struct d58c_context_view *target,
                 bool irq,bool if_clear,bool guard_clear)
{
    return d58c_c2b_irq_target_eligible(&tasks,&resources,&sync,
        src,source_frame,target,8,16,irq,if_clear,guard_clear);
}

/* Negative admission must leave entire input graph and both frame buffers
 * byte-identical. Copy all backing because a forged pointer must never
 * cause an out-of-bounds read or opportunistic "repair." */
static void immutable_deny(struct kernel_task_handle src,
                           const uint64_t *frame,
                           const struct d58c_context_view *target,
                           bool irq,bool if_clear,bool guard_clear)
{
    const struct kernel_task_table old_tasks=tasks;
    const struct kernel_task_resources old_resources=resources;
    const struct d53_sync old_sync=sync;
    unsigned char old_backing[sizeof backing];
    memcpy(old_backing,backing,sizeof backing);
    assert(!gate(src,frame,target,irq,if_clear,guard_clear));
    assert(memcmp(&old_tasks,&tasks,sizeof tasks)==0);
    assert(memcmp(&old_resources,&resources,sizeof resources)==0);
    assert(memcmp(&old_sync,&sync,sizeof sync)==0);
    assert(memcmp(old_backing,backing,sizeof backing)==0);
}

/* An authentic blocked high waiter has no saved IRETQ frame, even though
 * a real rank-0 release will wake it and make its M10-A state RUNNABLE. */
int main(void)
{
    assert(kernel_task_init(&tasks) &&
           kernel_task_resources_init(&resources) && d53_init(&sync));
    low=new_task(0,KERNEL_PRIORITY_BACKGROUND);
    struct kernel_task_handle chosen={0};
    assert(kernel_task_dispatch_selected(&tasks,low,&chosen)==KERNEL_TASK_OK);
    assert(d53_acquire(&sync,&resources,&tasks,0,low)==D53_OK);
    assert(kernel_task_yield(&tasks,low)==KERNEL_TASK_OK);
    high=new_task(1,KERNEL_PRIORITY_TIME_SENSITIVE);
    medium=new_task(2,KERNEL_PRIORITY_INTERACTIVE);
    assert(kernel_task_dispatch_selected(&tasks,high,&chosen)==KERNEL_TASK_OK);
    assert(d53_acquire(&sync,&resources,&tasks,0,high)==D53_BLOCKED);
    assert(tasks.current_slot==SIZE_MAX);
    assert(kernel_task_dispatch_selected(&tasks,low,&chosen)==KERNEL_TASK_OK);
    enum kernel_priority_class donated=KERNEL_PRIORITY_BACKGROUND;
    assert(d53_effective(&sync,&resources,&tasks,low,&donated)==D53_OK);
    assert(donated==KERNEL_PRIORITY_TIME_SENSITIVE);
    assert(tasks.tasks[medium.slot].state==KERNEL_TASK_RUNNABLE);
    assert(tasks.tasks[high.slot].state==KERNEL_TASK_BLOCKED);

    uint64_t *source_frame=synthetic_frame(0);
    uint64_t *medium_frame=synthetic_frame(2);
    struct d58c_context_view target={
        .handle=medium,
        .form=D58C_CONTEXT_VERIFIED_IRQ_IRETQ,
        .saved_irq_frame=medium_frame
    };
    struct d58c_context_view high_ret={
        .handle=high,.form=D58C_CONTEXT_COOPERATIVE_RET,
        .saved_irq_frame=NULL
    };
    assert(gate(low,source_frame,&target,true,true,true));
    immutable_deny(low,source_frame,&high_ret,true,true,true);
    immutable_deny(low,source_frame,&target,false,true,true);
    immutable_deny(low,source_frame,&target,true,false,true);
    immutable_deny(low,source_frame,&target,true,true,false);
    immutable_deny(low,NULL,&target,true,true,true);
    immutable_deny(low,source_frame,NULL,true,true,true);

    struct d58c_context_view corrupt=target;
    corrupt.form=D58C_CONTEXT_COOPERATIVE_RET;
    immutable_deny(low,source_frame,&corrupt,true,true,true);
    corrupt=target;
    corrupt.handle.generation++;
    immutable_deny(low,source_frame,&corrupt,true,true,true);
    corrupt=target;
    corrupt.saved_irq_frame=source_frame;
    immutable_deny(low,source_frame,&corrupt,true,true,true);
    corrupt=target;
    corrupt.saved_irq_frame=NULL;
    immutable_deny(low,source_frame,&corrupt,true,true,true);
    corrupt=target;
    corrupt.form=D58C_CONTEXT_INVALID;
    immutable_deny(low,source_frame,&corrupt,true,true,true);
    struct kernel_task_handle stale=low;
    stale.generation++;
    immutable_deny(stale,source_frame,&target,true,true,true);
    corrupt=target;
    corrupt.handle=low;
    immutable_deny(low,source_frame,&corrupt,true,true,true);

    uint64_t old_cs=medium_frame[M10_D2_CS];
    medium_frame[M10_D2_CS]=0;
    immutable_deny(low,source_frame,&target,true,true,true);
    medium_frame[M10_D2_CS]=old_cs;
    unsigned owned=resources.task[low.slot].owned_locks;
    ++resources.task[low.slot].owned_locks;
    immutable_deny(low,source_frame,&target,true,true,true);
    resources.task[low.slot].owned_locks=owned;

    /* M10-A changes the waiter from BLOCKED to READY and transfers lock
     * ownership, but its RET context is STILL not an IRQ-return target. */
    assert(d53_release(&sync,&resources,&tasks,0,low)==D53_OK);
    assert(tasks.tasks[high.slot].state==KERNEL_TASK_RUNNABLE);
    assert(resources.task[high.slot].registered_waiters==0);
    assert(resources.task[high.slot].owned_locks==1);
    immutable_deny(low,source_frame,&high_ret,true,true,true);
    assert(gate(low,source_frame,&target,true,true,true));
    puts("SPIFFOS_M10_D58C_C2B_CONTEXT_FORM_OK");
    return 0;
}
