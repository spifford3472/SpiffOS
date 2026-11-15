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
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
#include "m10-d58c-irq-frame.h"
#include "m10-d2-irq-frame.h"
#endif
#ifdef SPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST
#include "m10-d58b-admission.h"
/* Diagnostic-only IF=0 departure. Do not broaden the ordinary M10-B ABI. */
extern void x86_d58b_block_switch(struct x86_task_context *old_context,
                                  struct x86_task_context *surviving_context);
#endif
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
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
/* All C2a observations are read-only while the genuine IRQ0 hardware
 * frame is owned by the currently running M10-A low-priority task.
 * No IRQ-time scheduler handoff or mutex mutation occurs. */
static volatile bool c2_observing,c2_fault;
static volatile uint32_t c2_donated_irq_frames;
static uint16_t c2_cs,c2_ss;
#endif
#ifdef SPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST
static volatile unsigned verified_departures;
#endif

#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
/* R12 from the real x86-irq0.S stub points to the entire 20-qword
 * hardware-saved frame. This observer reads it only after exact owner
 * generation and D5.2b range/graph prevalidation; never writes the frame,
 * never changes M10-A, D5.3 or EOI. A fault latches permanently. */
void m10_d58c_c2_irq_observe(const uint64_t *frame)
{
    if(!c2_observing || c2_fault) return;
    uint64_t flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(flags)::"memory");
    struct kernel_task_handle owner=ids[0],high_id=ids[1],medium_id=ids[2];
    bool source=(flags&UINT64_C(0x200))==0 &&
        owner.generation!=0 && high_id.generation!=0 &&
        medium_id.generation!=0 &&
        owner.slot<D53_TASKS && high_id.slot<D53_TASKS &&
        medium_id.slot<D53_TASKS &&
        tasks.current_slot==owner.slot &&
        tasks.tasks[owner.slot].state==KERNEL_TASK_RUNNING &&
        tasks.tasks[high_id.slot].state==KERNEL_TASK_BLOCKED &&
        tasks.tasks[medium_id.slot].state==KERNEL_TASK_RUNNABLE &&
        resources.task[high_id.slot].registered_waiters==1 &&
        locks.locks[0].owned &&
        locks.locks[0].owner.slot==owner.slot &&
        locks.locks[0].owner.generation==owner.generation &&
        locks.locks[0].waiting==1 &&
        locks.locks[0].waiters[0].slot==high_id.slot &&
        locks.locks[0].waiters[0].generation==high_id.generation;
    enum kernel_priority_class donated=KERNEL_PRIORITY_BACKGROUND;
    if(!source) {
        c2_fault=true;reason="C2_IRQ_SOURCE";
        return;
    }
    if(!d58c_irq_frame_eligible(&tasks,&resources,&locks,owner,
           KERNEL_TASK_RUNNING,frame,c2_cs,c2_ss,true,true,true)) {
        c2_fault=true;reason="C2_IRQ_FRAME";
        return;
    }
    if(d53_effective(&locks,&resources,&tasks,owner,&donated)!=D53_OK ||
       donated!=KERNEL_PRIORITY_TIME_SENSITIVE) {
        c2_fault=true;reason="C2_IRQ_PRIORITY";
        return;
    }
    if(c2_donated_irq_frames!=UINT32_MAX) ++c2_donated_irq_frames;
}

/* Report actual hardware count only after leaving all worker stacks and
 * returning to the surviving bootstrap; serial is never called in IRQ. */
void m10_d58c_c2_report(bool (*write)(const char *))
{
    if(write==NULL) return;
    (void)write("D58C_C2_DONATED_IRQ_FRAMES:");
    char digits[12];
    unsigned pos=11;
    digits[pos]='\0';
    uint32_t value=c2_donated_irq_frames;
    do {digits[--pos]=(char)('0'+value%10u); value/=10u;} while(value!=0);
    (void)write(&digits[pos]);
    (void)write("\r\n");
}
#endif

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
#ifndef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
    irq_restore(flags);
#endif
    x86_task_switch(&context[i],&bootstrap);
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
    /* Actual cooperative return is possible only after a validated M10-A
     * RUNNABLE->RUNNING dispatcher selection on this exact saved stack. */
    irq_restore(flags);
#endif
}

/* Worker exit preserves its still-allocated stack until supervisor reaps. */
__attribute__((noreturn)) static void worker_exit(size_t i)
{
    uint64_t flags=irq_off();
    if(kernel_task_terminate(&tasks,ids[i])!=KERNEL_TASK_OK) failed=true;
#ifndef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
    irq_restore(flags);
#else
    (void)flags; /* Keep IF=0 until actual physical off-stack transfer. */
#endif
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
    /* Simulate a legitimate external-event wait while holding rank 0.
     * While this owner is BLOCKED, both independent mutex contenders may
     * queue. The surviving dispatcher wakes the owner after both block. */
    flags=irq_off();
    if(kernel_task_block(&tasks,ids[0])!=KERNEL_TASK_OK)
        failed=true;
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
    /* While the owner temporarily waits for an external event, keep IF=0
     * across the actual CPU-stack switch into the surviving dispatcher. */
    x86_d58b_block_switch(&context[0],&bootstrap);
    irq_restore(flags);
    /* The high-priority waiter was blocked on THIS lock on its own stack;
     * medium priority CPU competitor must still be READY. Wait for real
     * interrupt evidence before transferring the lock to high. */
    c2_observing=true;
    /* This old D5.4 fixture accepts a clock callback that may be
     * unavailable during boot; do not mistake absence of that optional
     * time source for a failed hardware IRQ0 observation. Bound CPU work
     * independently and let the guest harness enforce wall-time timeout. */
    for(uint32_t spins=0; spins<UINT32_C(200000000) &&
        !c2_fault && c2_donated_irq_frames<3; ++spins)
        __asm__ volatile("pause":::"memory");
    c2_observing=false;
    if(c2_fault || c2_donated_irq_frames<3) {
        failed=true;
        if(!c2_fault) reason="C2_IRQ_TIMEOUT";
    }
#else
    irq_restore(flags);
    x86_task_switch(&context[0],&bootstrap);
#endif
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
    stage[1]=1;
#ifdef SPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST
    /* D5.3 has made this exact task BLOCKED. Keep IF=0 all the way
     * through the physical stack transfer; never restore it beforehand. */
    if(!d58b_blocked_departure_metadata(&tasks,&resources,&locks,
                                          ids[1],false,true))
        {failed=true;reason="HIGH_BLOCK_GRAPH";}
    x86_d58b_block_switch(&context[1],&bootstrap);
    /* This continuation is reachable only after an explicit owner wake.
     * Restore the original worker IF, not a borrowed dispatcher flag. */
    irq_restore(flags);
#else
    irq_restore(flags);
    x86_task_switch(&context[1],&bootstrap);
#endif
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
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
    /* CPU competitor participates in M10-A READY queue but does not
     * claim any mutex. It retains an independent saved C continuation. */
    stage[2]=1;
    worker_yield(2);
    stage[2]=2;
    worker_exit(2);
#else
    uint64_t flags=irq_off();
    if(d53_acquire(&locks,&resources,&tasks,0,ids[2])!=D53_BLOCKED)
        failed=true;
    stage[2]=1;
#ifdef SPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST
    if(!d58b_blocked_departure_metadata(&tasks,&resources,&locks,
                                          ids[2],false,true))
        {failed=true;reason="MEDIUM_BLOCK_GRAPH";}
    x86_d58b_block_switch(&context[2],&bootstrap);
    irq_restore(flags);
#else
    irq_restore(flags);
    x86_task_switch(&context[2],&bootstrap);
#endif
    flags=irq_off();
    if(d53_release(&locks,&resources,&tasks,0,ids[2])!=D53_OK)
        failed=true;
    irq_restore(flags);
    stage[2]=2;
    worker_exit(2);
#endif
}

/* Allocate and bind three M2-owned 16KiB stacks. No owner is runnable
 * until binding, saved context and policy profiles all validate. */
bool m10_d54_guest_smoke(bool (*read_clock_ms)(uint64_t *))
{
    if(read_clock_ms==NULL) {reason="CLOCK";return false;}
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
    __asm__ volatile("movw %%cs,%0":"=r"(c2_cs));
    __asm__ volatile("movw %%ss,%0":"=r"(c2_ss));
#endif
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
        const size_t order[7]={0,1,2,0,1,2,0};
        if(selected.slot!=order[turn]) {
            reason="PRIORITY_ORDER";goto fail;
        }
#ifdef SPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST
        /* The original bootstrap IF is known before entering the task.
         * On a BLOCKED return it stays zero until these physical proofs
         * succeed; restore boot IF only AFTER checking the surviving RSP. */
        uint64_t bootstrap_if;
        __asm__ volatile("pushfq; popq %0":"=r"(bootstrap_if)::"memory");
        if((bootstrap_if&UINT64_C(0x200))==0) {
            reason="BOOT_IF_PRE";goto fail;
        }
#endif
        x86_task_switch(&bootstrap,&context[selected.slot]);
#ifdef SPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
        if(turn==0 || turn==1) {
#else
        if(turn==1 || turn==2) {
#endif
            uintptr_t observed_rsp;
            uint64_t live_flags;
            __asm__ volatile("movq %%rsp,%0":"=r"(observed_rsp)::"memory");
            __asm__ volatile("pushfq; popq %0":"=r"(live_flags)::"memory");
            bool independent=observed_rsp!=0;
            for(size_t j=0;j<D54_GUEST_WORKERS;++j) {
                uintptr_t bottom=(uintptr_t)stack[j];
                if(bottom>UINTPTR_MAX-X86_TASK_STACK_BYTES ||
                   (observed_rsp>=bottom &&
                    observed_rsp<bottom+X86_TASK_STACK_BYTES))
                    independent=false;
            }
            bool metadata_ok=tasks.current_slot==SIZE_MAX;
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
            if(turn==0)
                metadata_ok=metadata_ok &&
                    tasks.tasks[ids[0].slot].state==KERNEL_TASK_BLOCKED &&
                    resources.task[ids[0].slot].owned_locks==1;
            else
                metadata_ok=metadata_ok &&
                    d58b_blocked_departure_metadata(&tasks,&resources,
                                                     &locks,ids[1],false,true);
#else
            metadata_ok=metadata_ok &&
                d58b_blocked_departure_metadata(&tasks,&resources,&locks,
                                                  ids[selected.slot],false,true);
#endif
            if((live_flags&UINT64_C(0x200))!=0 || !independent ||
               !metadata_ok) {
                reason="BLOCKED_STACK_UNSAFE";
                /* Restore boot IF before following ordinary error path. */
                irq_restore(bootstrap_if);
                goto fail;
            }
            ++verified_departures;
        }
        irq_restore(bootstrap_if);
#endif
        flags=irq_off();
        enum kernel_task_resource_result saved=
            kernel_task_resources_saved_rsp(&resources,&tasks,selected,
                (uintptr_t)context[selected.slot].saved_rsp);
        bool consistent=d53_check(&locks,&resources,&tasks) &&
            kernel_task_check(&tasks);
        irq_restore(flags);
        if(saved!=KERNEL_RESOURCE_OK || !consistent || failed) {
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
            if(!failed) reason=saved!=KERNEL_RESOURCE_OK?"C2_RSP" :
                !consistent?"C2_GRAPH":"C2_WORKER";
#else
            reason="TASK_HANDOFF";
#endif
            goto fail;
        }
        if(turn==0) {
            flags=irq_off();
            ready=kernel_task_make_runnable(&tasks,ids[1])==KERNEL_TASK_OK &&
                kernel_task_make_runnable(&tasks,ids[2])==KERNEL_TASK_OK;
            irq_restore(flags);
            if(!ready) {reason="WAITER_ENQUEUE";goto fail;}
        }
        if(turn==2) {
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
            if(tasks.tasks[ids[0].slot].state!=KERNEL_TASK_BLOCKED ||
               tasks.tasks[ids[1].slot].state!=KERNEL_TASK_BLOCKED ||
               tasks.tasks[ids[2].slot].state!=KERNEL_TASK_RUNNABLE ||
               locks.locks[0].waiting!=1 ||
               resources.task[ids[1].slot].registered_waiters!=1 ||
               resources.task[ids[2].slot].registered_waiters!=0) {
#else
            if(tasks.tasks[ids[0].slot].state!=KERNEL_TASK_BLOCKED ||
               tasks.tasks[ids[1].slot].state!=KERNEL_TASK_BLOCKED ||
               tasks.tasks[ids[2].slot].state!=KERNEL_TASK_BLOCKED ||
               locks.locks[0].waiting!=2 ||
               resources.task[ids[1].slot].registered_waiters!=1 ||
               resources.task[ids[2].slot].registered_waiters!=1) {
#endif
                reason="TWO_BLOCKED";goto fail;
            }
            /* The external event has completed. Wake only the original
             * owner; donated priority is now consumed by D5.4 dispatch. */
            flags=irq_off();
            ready=kernel_task_wake(&tasks,ids[0])==KERNEL_TASK_OK;
            irq_restore(flags);
            if(!ready) {reason="OWNER_WAKE";goto fail;}
        }
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
        if(turn==3 &&
           (locks.locks[0].owner.slot!=ids[1].slot ||
            locks.locks[0].waiting!=0 ||
            tasks.tasks[ids[2].slot].state!=KERNEL_TASK_RUNNABLE ||
            c2_donated_irq_frames<3 || c2_fault)) {
#else
        if(turn==3 &&
           (locks.locks[0].owner.slot!=ids[1].slot ||
            locks.locks[0].waiting!=1 ||
            tasks.tasks[ids[2].slot].state!=KERNEL_TASK_BLOCKED)) {
#endif
            reason="FIRST_TRANSFER";goto fail;
        }
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
        if(turn==4 &&
           (locks.locks[0].owned ||
            tasks.tasks[ids[2].slot].state!=KERNEL_TASK_RUNNABLE)) {
#else
        if(turn==4 &&
           (locks.locks[0].owner.slot!=ids[2].slot ||
            tasks.tasks[ids[2].slot].state!=KERNEL_TASK_RUNNABLE)) {
#endif
            reason="SECOND_TRANSFER";goto fail;
        }
    }
#ifdef SPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST
    if(verified_departures!=2) {
        reason="MISSING_BLOCKED_WITNESS";goto fail;
    }
#endif
    if(!done[0] || !done[1] || !done[2] ||
       stage[0]!=3 || stage[1]!=2 || stage[2]!=2 ||
       locks.locks[0].owned || resources.task[0].owned_locks ||
       resources.task[1].owned_locks || resources.task[2].owned_locks ||
       resources.task[1].registered_waiters ||
       resources.task[2].registered_waiters ||
       tasks.ready_count!=0 || tasks.current_slot!=SIZE_MAX
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
       || c2_donated_irq_frames<3 || c2_fault
#endif
       ) {
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
#ifdef SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST
    /* C2a can fail after donating ownership or blocking high. In that
     * state a live lock/waiter still pins the backing, regardless of
     * whether the dispatcher has returned. Quarantine by halting the
     * isolated test with backing still owned, never free a referenced
     * task stack or synthesize an unblocked waiter. */
    for(size_t i=0;i<D54_GUEST_WORKERS;++i)
        if(resources.task[ids[i].slot].owned_locks!=0 ||
           resources.task[ids[i].slot].registered_waiters!=0)
            return false;
#endif
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
