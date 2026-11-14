/* Purpose: M10-D5.6 authoritative real IRQ0 M10-A handoff diagnostic.
 * Responsibilities: switch two actual non-yielding M10-A worker stacks and
 * the reserved supervisor via validated x86-64 IRETQ frames, while keeping
 * ALL task state owned by M10-A. Record completed supervisor service ticks.
 * Dependencies: kernel-task, D5.2b resource ledger, D5.5c frame verifier,
 * x86-task-context, normal M9 IRQ0 and M2 mapped stack allocator.
 * Invariants: only kernel_task_irq_handoff mutates task state from IRQ;
 * no allocation, mutex action, logging, EOI or resource reaping inside ISR.
 * Concurrency: one boot CPU; interrupt gate IF=0, no nested IRQ, no SIMD.
 * Reference: ADR-m10-d56-irq-handoff.md and AGENTS.md. */
#include "kernel-task.h"
#include "kernel-task-resources.h"
#include "m10-d55c-frame-owner.h"
#include "m10-d2-irq-frame.h"
#include "x86-task-context.h"
#include "memory-boot.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef SPIFFOS_M10_D56_IRQ_TEST
enum { D56_WORKERS=2, D56_STACKS=3, D56_SUP_INDEX=2,
       D56_CANARY=64, D56_STOP_TICKS=120,
       D56_MAX_SERVICE_GAP=8, D56_MIN_WORKER_TICKS=12 };

static struct kernel_task_table tasks;
static struct kernel_task_resources owners;
static struct kernel_task_handle handles[D56_STACKS];
static struct x86_task_context supervisor_context,boot_context;
static uint8_t *stacks[D56_STACKS];
static uint64_t *saved_frames[D56_STACKS];
static uint64_t expected_cs,expected_ss;
static struct d55c_tick_service service;
static volatile uint64_t iterations[D56_WORKERS];
static volatile uint32_t slices[D56_STACKS],ticks,irq_switches;
static volatile uint32_t critical_depth,critical_cycles;
static volatile bool active,done,failed,returned_to_boot;
static bool negative_rejected;
static const char *reason="UNSET";

/* Diagnostic reason is read only on the non-interrupted boot context. */
const char *m10_d56_failure_reason(void) {return reason;}

/* The exclusive critical section's old IF must be restored exactly.
 * Nesting protects lock-like state transitions from tick preemption:
 * no IRQ0 may observe intermediate mutation when IF=0. */
static uint64_t enter_critical(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    ++critical_depth;
    return flags;
}

/* LIFO matched critical exit, with fail-closed underflow detection. */
static void exit_critical(uint64_t previous_flags)
{
    if(critical_depth==0) failed=true;
    else --critical_depth;
    if((previous_flags&UINT64_C(0x200))!=0)
        __asm__ volatile("sti":::"memory");
}

/* Canonical address check covers the four-level QEMU test paging mode. */
static bool canonical(uint64_t address)
{
    return (address>>48)==(((address>>47)&1u)!=0 ?
        UINT64_C(0xffff):UINT64_C(0));
}

/* Prove complete saved 20-qword frame lies on the independently bound
 * generation's owned stack before dereferencing any frame word. This is
 * also used for READY targets, unlike D5.5c's RUNNING-only verifier. */
static bool frame_safe(unsigned index,const uint64_t *frame,
                       enum kernel_task_state required)
{
    if(index>=D56_STACKS || frame==NULL ||
       !kernel_task_resources_check(&owners,&tasks))
        return false;
    const struct kernel_task_handle h=handles[index];
    unsigned slot=h.slot;
    if(slot>=KERNEL_TASK_CAPACITY ||
       tasks.tasks[slot].generation!=h.generation ||
       tasks.tasks[slot].state!=required)
        return false;
    const struct kernel_task_resource_record *r=&owners.task[slot];
    if(!r->installed || r->quarantined ||
       r->handle.slot!=h.slot || r->handle.generation!=h.generation ||
       r->owned_locks!=0 || r->registered_waiters!=0)
        return false;
    uintptr_t start=r->stack.bottom, ptr=(uintptr_t)frame;
    size_t capacity=r->stack.bytes;
    const size_t nbytes=M10_D2_FRAME_QWORDS*sizeof(uint64_t);
    if((ptr&7u)!=0 || capacity<nbytes || start>UINTPTR_MAX-capacity ||
       ptr<start || ptr-start>capacity-nbytes)
        return false;
    const uintptr_t top=start+capacity;
    const uint64_t rip=frame[M10_D2_RIP],rsp=frame[M10_D2_RSP],
        flags=frame[M10_D2_RFLAGS];
    return canonical(rip) && canonical(rsp) &&
        (flags&UINT64_C(0x202))==UINT64_C(0x202) &&
        frame[M10_D2_CS]==expected_cs &&
        frame[M10_D2_SS]==expected_ss &&
        rsp>ptr+nbytes-1u && rsp<top && rsp>=start;
}

/* A single helper maps M10-A supervisor slot 15 back to its actual stack.
 * The mapping is a static fixture configuration, never another task table. */
static unsigned index_of_slot(size_t slot)
{
    if(slot==handles[0].slot) return 0;
    if(slot==handles[1].slot) return 1;
    if(slot==handles[D56_SUP_INDEX].slot) return D56_SUP_INDEX;
    return D56_STACKS;
}

/* An interrupted worker must validate with D5.5c's authoritative source
 * verifier, while the supervisor validates its exclusive reserved M10-A
 * descriptor and independently M2-owned supervisor stack. */
static bool incoming_safe(unsigned index,const uint64_t *frame)
{
    if(!frame_safe(index,frame,KERNEL_TASK_RUNNING)) return false;
    if(index==D56_SUP_INDEX) return true;
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0":"=r"(flags)::"memory");
    struct d55c_irq_snapshot evidence={
        .current=handles[index],.saved_frame=frame,
        .expected_cs=expected_cs,.expected_ss=expected_ss,
        .cpu_if_clear=(flags&UINT64_C(0x200))==0,
        .irq_depth=1,.critical_depth=critical_depth
    };
    return d55c_frame_matches(&tasks,&owners,&evidence);
}

/* Only the existing IRQ0 entry stub supplies the authentic CPU frame,
 * after exactly one ordinary timer callback and EOI. The returned frame
 * is either the source or a fully verified saved frame of a READY owner.
 * No extra timer EOI or second scheduler is introduced. */
uint64_t *m10_d56_irq_select(uint64_t *incoming)
{
    if(!active) return incoming;
    const unsigned from=index_of_slot(tasks.current_slot);
    if(from>=D56_STACKS || critical_depth!=0 ||
       !incoming_safe(from,incoming)) {
        failed=true;reason="UNSAFE_CURRENT_FRAME";active=false;
        /* Unknown source is never used for a speculative task handoff. */
        return incoming;
    }
    saved_frames[from]=incoming;
    if(ticks!=UINT32_MAX) ++ticks;
    if(slices[from]!=UINT32_MAX) ++slices[from];
    if(!d55c_tick_observe(&service,true,ticks))
        {failed=true;reason="SUPERVISOR_DEADLINE";}
    if(ticks>=D56_STOP_TICKS) done=true;

    /* The only candidate authority is M10-A's own FIFO ready queue.
     * Rotation through supervisor gives service within three real ticks. */
    unsigned to=D56_STACKS;
    if(!done && !failed && tasks.ready_count!=0)
        to=index_of_slot(tasks.ready_queue[0]);
    else to=D56_SUP_INDEX;

#ifdef SPIFFOS_M10_D56_BAD_TARGET_TEST
    /* Explicit negative changes the EXPECTED target frame's CS, never
     * an interrupted source frame or the authoritative task identity. */
    if(from==0 && !negative_rejected && to==1) {
        saved_frames[1][M10_D2_CS]=0;
    }
#endif
#ifdef SPIFFOS_M10_D56_BAD_CRITICAL_TEST
    /* Test-only forged nested critical depth. It must reject BEFORE M10-A
     * state changes and route to the already verified supervisor stack. */
    if(from==0 && !negative_rejected) {
        critical_depth=1;
    }
#endif
    if(to>=D56_STACKS) {
        failed=true;reason="NO_READY";to=D56_SUP_INDEX;
    }
    if(critical_depth!=0 ||
       (to!=from && !frame_safe(to,saved_frames[to],
                               KERNEL_TASK_RUNNABLE))) {
        negative_rejected=true;
        failed=true;
        reason=critical_depth!=0?"CRITICAL_DENIED":"TARGET_FRAME_DENIED";
        critical_depth=0;
        to=D56_SUP_INDEX;
    }
    if(to==from) return incoming;
    /* A failing worker can be routed to the prevalidated supervisor
     * without ever executing a rejected next frame. No forced lock release. */
    if(!frame_safe(to,saved_frames[to],KERNEL_TASK_RUNNABLE)) {
        failed=true;reason="NO_SAFE_SUPERVISOR";
        active=false;
        return incoming;
    }
    struct kernel_task_handle chosen={0};
    if(kernel_task_irq_handoff(&tasks,handles[from],handles[to],
                              &chosen)!=KERNEL_TASK_OK ||
       chosen.slot!=handles[to].slot ||
       chosen.generation!=handles[to].generation) {
        failed=true;reason="M10_A_HANDOFF";
        active=false;
        return incoming;
    }
    if(irq_switches!=UINT32_MAX) ++irq_switches;
    return saved_frames[to];
}

/* A worker genuinely consumes CPU without voluntarily yielding. It performs
 * nested IF=0 critical sections, proving no IRQ selector can enter while
 * the shared critical-depth counter is nonzero. */
__attribute__((noreturn)) static void worker_run(unsigned index)
{
    volatile uint64_t work=(uint64_t)index+1u;
    while(!done && !failed) {
        work=work*UINT64_C(6364136223846793005)+UINT64_C(1);
        ++iterations[index];
        if((iterations[index]&UINT64_C(0x3fff))==0) {
            uint64_t outer=enter_critical();
            uint64_t inner=enter_critical();
            for(unsigned i=0;i<40;++i)
                work=work*UINT64_C(3)+1u;
            ++critical_cycles;
            exit_critical(inner);
            exit_critical(outer);
        }
        __asm__ volatile("pause":::"memory");
    }
    /* IRQ scheduler always routes back to supervisor on completion; a
     * worker falling through here indicates unsafe lifecycle handling. */
    failed=true;
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}
__attribute__((noreturn)) static void worker_zero(void) {worker_run(0);}
__attribute__((noreturn)) static void worker_one(void) {worker_run(1);}

/* Supervisor executes on a third, registered stack and records only
 * completed work, never mere opportunities chosen by the IRQ scheduler. */
__attribute__((noreturn)) static void supervisor_run(void)
{
    /* Enable the IRQ selector ONLY after cooperative first-entry has
     * established the actual supervisor-owned stack and exact handle. */
    const uint64_t entry=enter_critical();
    active=true;
    exit_critical(entry);
    while(!done && !failed) {
        const uint64_t prior=enter_critical();
        if(!d55c_tick_service_completed(&service,true,true,ticks)) {
            failed=true;reason="SERVICE_COMPLETION";
        }
        exit_critical(prior);
        __asm__ volatile("pause":::"memory");
    }
    const uint64_t prior=enter_critical();
    if(kernel_task_terminate(&tasks,handles[D56_SUP_INDEX])!=KERNEL_TASK_OK) {
        failed=true;reason="SUPERVISOR_EXIT";
    }
    exit_critical(prior);
    returned_to_boot=true;
    x86_task_switch(&supervisor_context,&boot_context);
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}

/* Build an initial IRQ IRETQ frame on a verified M2 stack; its saved
 * CPU RSP is the 8-mod-16 C entry position, not the 160-byte frame ptr. */
static bool prepare_worker(unsigned index,void (*entry)(void))
{
    const uintptr_t base=(uintptr_t)stacks[index];
    if(index>=D56_WORKERS || base==0 ||
       base>UINTPTR_MAX-X86_TASK_STACK_BYTES ||
       (base&15u)!=0 || entry==NULL) return false;
    const uintptr_t top=base+X86_TASK_STACK_BYTES;
    const uintptr_t rsp=top-8u;
    uint64_t *words=(uint64_t *)(rsp-
        M10_D2_FRAME_QWORDS*sizeof(uint64_t));
    for(size_t i=0;i<M10_D2_FRAME_QWORDS;++i) words[i]=0;
    *(uint64_t *)rsp=0;
    words[M10_D2_RIP]=(uintptr_t)entry;
    words[M10_D2_CS]=expected_cs;
    words[M10_D2_RFLAGS]=UINT64_C(0x202);
    words[M10_D2_RSP]=rsp;
    words[M10_D2_SS]=expected_ss;
    saved_frames[index]=words;
    return true;
}

/* All M10-A task handles and exact physical stack-owner registrations
 * are established before a single IRQ is authorized to select a worker. */
bool m10_d56_guest_smoke(bool (*clock_ms)(uint64_t *))
{
    if(clock_ms==NULL) {reason="CLOCK";return false;}
    const uint64_t entry=enter_critical();
    if((entry&UINT64_C(0x200))==0) {
        reason="IF_DISABLED";return false;
    }
    unsigned prepared=0;
    for(;prepared<D56_STACKS;++prepared) {
        void *owned=NULL;
        if(!memory_boot_m10_task_stack_prepare(prepared,&owned)) break;
        stacks[prepared]=(uint8_t *)owned;
    }
    exit_critical(entry);
    if(prepared!=D56_STACKS) {reason="STACK_ALLOC";goto fail_early;}
    if(!kernel_task_init(&tasks) || !kernel_task_resources_init(&owners) ||
       !d55c_tick_init(&service,0,D56_MAX_SERVICE_GAP)) {
        reason="INIT";goto fail_early;
    }
    uint16_t cs=0,ss=0;
    __asm__ volatile("movw %%cs,%0":"=r"(cs));
    __asm__ volatile("movw %%ss,%0":"=r"(ss));
    expected_cs=cs;expected_ss=ss;
    for(unsigned i=0;i<D56_STACKS;++i)
        for(size_t j=0;j<X86_TASK_STACK_BYTES;++j)
            stacks[i][j]=UINT8_C(0xa7);
    if(!prepare_worker(0,worker_zero) ||
       !prepare_worker(1,worker_one) ||
       !x86_task_context_init(&supervisor_context,
            stacks[D56_SUP_INDEX],X86_TASK_STACK_BYTES,supervisor_run)) {
        reason="FRAME_INIT";goto fail_early;
    }
    /* Bootstrap cooperative supervisor context is also registered as
     * an exact M10-A generation; its hardware IRQ frame appears upon the
     * first timer interrupt *after* switching onto its owned stack. */
    const uint64_t flags=enter_critical();
    bool ready=kernel_task_create(&tasks,true,&handles[D56_SUP_INDEX])==
               KERNEL_TASK_OK &&
        kernel_task_make_runnable(&tasks,handles[D56_SUP_INDEX])==
               KERNEL_TASK_OK;
    for(unsigned i=0;i<D56_WORKERS && ready;++i)
        ready=kernel_task_create(&tasks,false,&handles[i])==KERNEL_TASK_OK &&
            kernel_task_make_runnable(&tasks,handles[i])==KERNEL_TASK_OK;
    for(unsigned i=0;i<D56_STACKS && ready;++i) {
        const struct kernel_task_stack_binding binding={
            .bottom=(uintptr_t)stacks[i],.bytes=X86_TASK_STACK_BYTES,
            .saved_rsp=i==D56_SUP_INDEX ?
                (uintptr_t)supervisor_context.saved_rsp:
                (uintptr_t)saved_frames[i],
            .owner_index=i
        };
        ready=kernel_task_resources_bind(&owners,&tasks,handles[i],
            &binding)==KERNEL_RESOURCE_OK;
    }
    struct kernel_task_handle dispatched={0};
    if(ready) ready=kernel_task_dispatch(&tasks,&dispatched)==KERNEL_TASK_OK &&
        dispatched.slot==handles[D56_SUP_INDEX].slot;
    exit_critical(flags);
    if(!ready) {reason="M10_A_SETUP";goto fail_early;}

    /* Switching from boot to supervisor must be atomic relative to IRQ0.
     * Cooperative task_switch does not change IF, so the first IRQ after
     * this call can occur only after supervisor has begun executing. */
    /* IRQ selector remains inactive while still on bootstrap stack.
     * Supervisor enables it only after the cooperative stack handoff. */
    x86_task_switch(&boot_context,&supervisor_context);
    /* Supervisor is now TERMINATED and switched completely off its stack. */
    active=false;
    const uint64_t clean=enter_critical();
    uintptr_t actual_rsp;
    __asm__ volatile("mov %%rsp,%0":"=r"(actual_rsp)::"memory");
    bool good=returned_to_boot && done && !failed &&
        irq_switches>=D56_MIN_WORKER_TICKS &&
        slices[0]>=D56_MIN_WORKER_TICKS &&
        slices[1]>=D56_MIN_WORKER_TICKS &&
        service.service_completions>=D56_MIN_WORKER_TICKS &&
        service.max_observed_gap_ticks<=D56_MAX_SERVICE_GAP &&
        critical_cycles!=0 &&
        kernel_task_check(&tasks) &&
        kernel_task_resources_check(&owners,&tasks);
    for(unsigned i=0;i<D56_WORKERS;++i)
        if(kernel_task_terminate(&tasks,handles[i])!=KERNEL_TASK_OK)
            good=false;
    for(unsigned i=0;i<D56_STACKS;++i) {
        /* Last saved RSP is physical registered backing, not an untrusted
         * worker-supplied address; update only after switching to boot. */
        uintptr_t last_rsp=i==D56_SUP_INDEX ?
            (uintptr_t)supervisor_context.saved_rsp :
            (uintptr_t)saved_frames[i];
        if(kernel_task_resources_saved_rsp(&owners,&tasks,
                handles[i],last_rsp)!=KERNEL_RESOURCE_OK) good=false;
    }
    for(unsigned i=0;i<D56_STACKS;++i) {
        if(kernel_task_resources_retire(&owners,&tasks,handles[i],
               actual_rsp,memory_boot_m10_task_stack_release)!=
                   KERNEL_RESOURCE_OK) good=false;
    }
    exit_critical(clean);
    if(!good) {reason=failed?reason:"QUOTA_OR_REAP";return false;}
    return true;
fail_early:
    /* No task has executed yet; all allocated backing can be safely
     * returned from the boot stack. Never repair an active IRQ owner. */
    ; /* A statement before local declarations keeps strict C11. */
    const uint64_t rollback=enter_critical();
    for(unsigned i=prepared;i>0;--i)
        (void)memory_boot_m10_task_stack_release(i-1);
    exit_critical(rollback);
    return false;
}
#endif
