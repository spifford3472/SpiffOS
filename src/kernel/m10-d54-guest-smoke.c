/* Purpose: D5.4 real three-worker multiwaiter dispatch proof in QEMU.
 * Responsibilities: actual cooperative blocked-stack handoff, effective
 * priority selection, two separate waiter resumes, off-stack reclamation.
 * Dependencies: M10-A/D5.2b/D5.3/D5.4, M10-B x86 task switch and M2 memory.
 * Invariants: each waiter resumes only after exact owner transfer; physical
 * stacks freed only by surviving boot dispatcher after all three exits.
 * Concurrency: one CPU, IF=0 only for state/owner mutation, no IRQ switch.
 * References: ADR-m10-d54-priority-dispatch.md, AGENTS.md. */
#include "kernel-dispatch-d54.h"
#include "kernel-task-cleanup.h"
#include "memory-boot.h"
#include "x86-task-context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SPIFFOS_M10_D54_GUEST_TEST
enum { D54_GUEST_WORKERS=3 };
static struct kernel_task_table tasks;
static struct kernel_task_resources resources;
static struct d53_sync locks;
static struct d54_dispatch policy;
static struct kernel_task_cleanup_queue cleanup;
static struct kernel_task_handle ids[D54_GUEST_WORKERS];
static struct x86_task_context bootstrap;
static struct x86_task_context context[D54_GUEST_WORKERS];
static uint8_t *stack[D54_GUEST_WORKERS];
static volatile bool failed;
static volatile bool done[D54_GUEST_WORKERS];
static volatile unsigned stage[D54_GUEST_WORKERS];
static const char *reason="UNSET";

/* Static diagnostic failure labels remain accessible after stack failure. */
const char *m10_d54_guest_failure_reason(void) {return reason;}

/* Preserve the actual entry IF status; do not change boot-wide policy. */
static uint64_t irq_off(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    return flags;
}
/* Only restore IF if it was enabled before this critical section. */
static void irq_restore(uint64_t flags)
{
    if((flags & UINT64_C(0x200))!=0)
        __asm__ volatile("sti":::"memory");
}

/* Policy yield occurs before returning execution to permanent boot stack. */
static void worker_yield(size_t i)
{
    uint64_t flags=irq_off();
    if(kernel_task_yield(&tasks,ids[i])!=KERNEL_TASK_OK) failed=true;
    irq_restore(flags);
    x86_task_switch(&context[i],&bootstrap);
}

/* Worker exit preserves its still-allocated stack until supervisor reaps. */
__attribute__((noreturn)) static void worker_exit(size_t i)
{
    uint64_t flags=irq_off();
    if(kernel_task_terminate(&tasks,ids[i])!=KERNEL_TASK_OK) failed=true;
    irq_restore(flags);
    done[i]=true;
    x86_task_switch(&context[i],&bootstrap);
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}

/* Low-priority owner first acquires and yields. It must be boosted after
 * both higher-priority waiters block, then transfer first ownership. */
__attribute__((noreturn)) static void owner(void)
{
    uint64_t flags=irq_off();
    if(d53_acquire(&locks,&resources,&tasks,0,ids[0])!=D53_OK)
        failed=true;
    irq_restore(flags);
    stage[0]=1;
    worker_yield(0);
    flags=irq_off();
    enum kernel_priority_class effective=KERNEL_PRIORITY_BACKGROUND;
    if(d53_effective(&locks,&resources,&tasks,ids[0],&effective)!=D53_OK ||
       effective!=KERNEL_PRIORITY_TIME_SENSITIVE ||
       d53_release(&locks,&resources,&tasks,0,ids[0])!=D53_OK)
        failed=true;
    irq_restore(flags);
    stage[0]=2;
    worker_yield(0);
    stage[0]=3;
    worker_exit(0);
}

/* High-priority waiter is the first contender; its C continuation MUST
 * remain suspended until owner transfers lock and real context resumes. */
__attribute__((noreturn)) static void high(void)
{
    uint64_t flags=irq_off();
    if(d53_acquire(&locks,&resources,&tasks,0,ids[1])!=D53_BLOCKED)
        failed=true;
    irq_restore(flags);
    stage[1]=1;
    x86_task_switch(&context[1],&bootstrap);
    flags=irq_off();
    if(d53_release(&locks,&resources,&tasks,0,ids[1])!=D53_OK)
        failed=true;
    irq_restore(flags);
    stage[1]=2;
    worker_exit(1);
}

/* Medium waiter is queued behind high and remains BLOCKED when high
 * is woken; its separate saved stack continuation runs after next handoff. */
__attribute__((noreturn)) static void medium(void)
{
    uint64_t flags=irq_off();
    if(d53_acquire(&locks,&resources,&tasks,0,ids[2])!=D53_BLOCKED)
        failed=true;
    irq_restore(flags);
    stage[2]=1;
    x86_task_switch(&context[2],&bootstrap);
    flags=irq_off();
    if(d53_release(&locks,&resources,&tasks,0,ids[2])!=D53_OK)
        failed=true;
    irq_restore(flags);
    stage[2]=2;
    worker_exit(2);
}

/* Allocate and bind three M2-owned 16KiB stacks. No owner is runnable
 * until binding, saved context and policy profiles all validate. */
bool m10_d54_guest_smoke(bool (*read_clock_ms)(uint64_t *))
{
    if(read_clock_ms==NULL) {reason="CLOCK";return false;}
    uint64_t flags=irq_off();
    if((flags & UINT64_C(0x200))==0) {reason="IF_DISABLED";return false;}
    if(!memory_boot_m10_task_stack_failure_probe()) {
        irq_restore(flags);reason="ROLLBACK";return false;
    }
    bool ready=true;
    for(size_t i=0;i<D54_GUEST_WORKERS;++i) {
        void *owned=NULL;
        if(!memory_boot_m10_task_stack_prepare(i,&owned)) {
            ready=false;break;
        }
        stack[i]=(uint8_t *)owned;
    }
    irq_restore(flags);
    if(!ready) {reason="STACK_PREPARE";goto fail;}
    bool initialized=kernel_task_init(&tasks) &&
        kernel_task_resources_init(&resources) && d53_init(&locks) &&
        d54_init(&policy) && kernel_task_cleanup_init(&cleanup);
    if(!initialized) {reason="INIT";goto fail;}
    void (*entry[D54_GUEST_WORKERS])(void)={owner,high,medium};
    for(size_t i=0;i<D54_GUEST_WORKERS;++i) {
        for(size_t b=0;b<X86_TASK_STACK_BYTES;++b)
            stack[i][b]=UINT8_C(0x5a);
        if(!x86_task_context_init(&context[i],stack[i],
                X86_TASK_STACK_BYTES,entry[i])) {
            reason="CONTEXT";goto fail;
        }
    }
    flags=irq_off();
    for(size_t i=0;i<D54_GUEST_WORKERS;++i) {
        struct kernel_task_stack_binding binding={
            .bottom=(uintptr_t)stack[i],
            .bytes=X86_TASK_STACK_BYTES,
            .saved_rsp=(uintptr_t)context[i].saved_rsp,
            .owner_index=i
        };
        enum kernel_priority_class base=i==0 ?
            KERNEL_PRIORITY_BACKGROUND :
            (i==1 ? KERNEL_PRIORITY_TIME_SENSITIVE :
                    KERNEL_PRIORITY_INTERACTIVE);
        if(kernel_task_create(&tasks,false,&ids[i])!=KERNEL_TASK_OK ||
           kernel_task_resources_bind(&resources,&tasks,ids[i],
               &binding)!=KERNEL_RESOURCE_OK ||
           d53_enroll(&locks,&resources,&tasks,ids[i],base)!=D53_OK) {
            ready=false;break;
        }
    }
    if(ready)
        ready=kernel_task_make_runnable(&tasks,ids[0])==KERNEL_TASK_OK;
    irq_restore(flags);
    if(!ready) {reason="TASK_BIND";goto fail;}
    uint64_t begin=0,end=0;
    bool clock=read_clock_ms(&begin);
    /* First execution creates and yields low owner. Two contenders are
     * not eligible before that event, so no unnatural priority inversion
     * is required to manufacture the multiwaiter proof. */
    for(size_t turn=0;turn<7;++turn) {
        struct kernel_task_handle selected={0};
        flags=irq_off();
        enum kernel_task_result chose=d54_dispatch_next(
            &policy,&locks,&resources,&tasks,&selected);
        irq_restore(flags);
        if(chose!=KERNEL_TASK_OK || selected.slot>=D54_GUEST_WORKERS) {
            reason="DISPATCH";goto fail;
        }
        const unsigned expected[D54_GUEST_WORKERS]={0,0,0};
        (void)expected;
        const size_t order[7]={0,1,2,0,1,2,0};
        if(selected.slot!=order[turn]) {
            reason="PRIORITY_ORDER";goto fail;
        }
        x86_task_switch(&bootstrap,&context[selected.slot]);
        flags=irq_off();
        enum kernel_task_resource_result saved=
            kernel_task_resources_saved_rsp(&resources,&tasks,selected,
                (uintptr_t)context[selected.slot].saved_rsp);
        bool consistent=d53_check(&locks,&resources,&tasks) &&
            kernel_task_check(&tasks);
        irq_restore(flags);
        if(saved!=KERNEL_RESOURCE_OK || !consistent || failed) {
            reason="TASK_HANDOFF";goto fail;
        }
        if(turn==0) {
            flags=irq_off();
            ready=kernel_task_make_runnable(&tasks,ids[1])==KERNEL_TASK_OK &&
                kernel_task_make_runnable(&tasks,ids[2])==KERNEL_TASK_OK;
            irq_restore(flags);
            if(!ready) {reason="WAITER_ENQUEUE";goto fail;}
        }
        if(turn==2 &&
           (tasks.tasks[ids[1].slot].state!=KERNEL_TASK_BLOCKED ||
            tasks.tasks[ids[2].slot].state!=KERNEL_TASK_BLOCKED ||
            locks.locks[0].waiting!=2 ||
            resources.task[ids[1].slot].registered_waiters!=1 ||
            resources.task[ids[2].slot].registered_waiters!=1)) {
            reason="TWO_BLOCKED";goto fail;
        }
        if(turn==3 &&
           (locks.locks[0].owner.slot!=ids[1].slot ||
            locks.locks[0].waiting!=1 ||
            tasks.tasks[ids[2].slot].state!=KERNEL_TASK_BLOCKED)) {
            reason="FIRST_TRANSFER";goto fail;
        }
        if(turn==4 &&
           (locks.locks[0].owner.slot!=ids[2].slot ||
            tasks.tasks[ids[2].slot].state!=KERNEL_TASK_RUNNABLE)) {
            reason="SECOND_TRANSFER";goto fail;
        }
    }
    if(!done[0] || !done[1] || !done[2] ||
       stage[0]!=3 || stage[1]!=2 || stage[2]!=2 ||
       locks.locks[0].owned || resources.task[0].owned_locks ||
       resources.task[1].owned_locks || resources.task[2].owned_locks ||
       resources.task[1].registered_waiters ||
       resources.task[2].registered_waiters ||
       tasks.ready_count!=0 || tasks.current_slot!=SIZE_MAX) {
        reason="FINAL_REFS";goto fail;
    }
    if(clock && (!read_clock_ms(&end) || end<begin)) {
        reason="CLOCK_BACK";goto fail;
    }
    for(size_t i=0;i<D54_GUEST_WORKERS;++i)
        for(size_t b=0;b<64;++b)
            if(stack[i][b]!=UINT8_C(0x5a)) {
                reason="STACK_CANARY";goto fail;
            }
    /* Every worker is terminated and switched off-stack before any memory
     * release; only the boot dispatcher calls bounded supervisor queue. */
    for(size_t i=0;i<D54_GUEST_WORKERS;++i) {
        flags=irq_off();
        uintptr_t boot_rsp;
        __asm__ volatile("mov %%rsp,%0":"=r"(boot_rsp)::"memory");
        enum kernel_cleanup_result enqueued=kernel_task_cleanup_enqueue(
            &cleanup,&resources,&tasks,ids[i]);
        enum kernel_cleanup_result retired=enqueued==KERNEL_CLEANUP_OK ?
            kernel_task_cleanup_service(&cleanup,&resources,&tasks,
                boot_rsp,memory_boot_m10_task_stack_release):
            KERNEL_CLEANUP_INVALID;
        irq_restore(flags);
        if(retired!=KERNEL_CLEANUP_OK) {
            reason="REAPER";return false;
        }
    }
    if(cleanup.count!=0 || cleanup.completed!=D54_GUEST_WORKERS) {
        reason="QUEUE";return false;
    }
    return true;
fail:
    /* Diagnostic failure never resumes a task. Do not free any backing
     * if execution might still be on it: the caller is on boot stack. */
    flags=irq_off();
    bool clean=true;
    for(size_t i=0;i<D54_GUEST_WORKERS;++i)
        if(stack[i]!=NULL &&
           !memory_boot_m10_task_stack_release(i))
            clean=false;
    irq_restore(flags);
    if(!clean) reason="UNSAFE_ROLLBACK";
    return false;
}
#endif
