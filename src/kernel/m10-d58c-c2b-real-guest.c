/* Purpose: C2b isolated real IRQ0 donated-mutex and mixed x86 context proof.
 * Responsibilities: execute a true BLOCKED high RET waiter, low lock owner,
 * preempted medium IRQ CPU worker, reserved IRQ supervisor and safe off-ramp.
 * Dependencies: M10-A, D5.2b, D5.3, D5.7, D5.8b/C1/C2b, M2 task stacks.
 * Invariants: M10-A alone owns state; IRQ0 only validates frame/graph and
 * calls exact M10-A handoff; no D5.3 mutation, allocator, EOI or I/O in IRQ.
 * A RET waiter is never an IRETQ target even after M10-A wakes it.
 * Concurrency: one QEMU boot CPU, IF=0 for every state/lock transition;
 * default kernel and prior diagnostic tests never link this object.
 * References: ADR-m10-d58c-c2b-real-irq-bridge.md and AGENTS.md. */
#include "kernel-task.h"
#include "kernel-task-resources.h"
#include "kernel-sync-d53.h"
#include "m10-d57-safety.h"
#include "m10-d58b-admission.h"
#include "m10-d58c-irq-frame.h"
#include "m10-d58c-c2b-resume-gate.h"
#include "m10-d55c-frame-owner.h"
#include "m10-d2-irq-frame.h"
#include "memory-boot.h"
#include "x86-task-context.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef SPIFFOS_M10_D58C_C2B_REAL_IRQ_TEST
enum { C2B_LOW=0, C2B_HIGH=1, C2B_MEDIUM=2,
       C2B_SUPERVISOR=3, C2B_STACKS=4,
       C2B_STOP_TICKS=220, C2B_MIN_SLICES=12,
       C2B_MAX_SERVICE_GAP=8, C2B_CANARY_BYTES=64 };
static struct kernel_task_table tasks;
static struct kernel_task_resources resources;
static struct d53_sync sync;
static struct d57_irq_policy policy;
static struct d57_preempt_guard guard;
static struct d55c_tick_service service;
static struct kernel_task_handle ids[C2B_STACKS];
static struct x86_task_context boot,cooperative[2];
static uint8_t *stacks[C2B_STACKS];
static uint64_t *irq_frame[C2B_STACKS];
static uint16_t expected_cs,expected_ss;
static volatile uint32_t ticks,slices[C2B_STACKS],irq_switches;
static volatile uint32_t donated_medium_to_low;
static volatile uint64_t medium_work,supervisor_work;
static volatile bool active,failed,phase_finished,low_done,high_done;
#if defined(SPIFFOS_M10_D58C_C2B_BAD_TARGET_TEST) || \
    defined(SPIFFOS_M10_D58C_C2B_BAD_GENERATION_TEST)
static volatile bool negative_rejected;
#endif
static const char *reason="UNSET";

/* Dedicated diagnostic-only IF-off context swap, not the normal kernel
 * M10-B interface. The caller proves the target is an independent stack. */
extern void x86_d58b_block_switch(struct x86_task_context *old_context,
                                  struct x86_task_context *surviving_context);

/* Plain fixed-width reason is read only after returning to bootstrap. */
const char *m10_d58c_c2b_real_failure_reason(void) {return reason;}

/* Save actual current CPU IF and mask future IRQ until caller completes a
 * bounded M10-A/D5.3 operation. Restore only the previously enabled IF. */
static uint64_t irq_off(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    return flags;
}
static void irq_restore(uint64_t flags)
{
    if((flags&UINT64_C(0x200))!=0)
        __asm__ volatile("sti":::"memory");
}

/* All frame buffers are permanent 16KiB mapped, separately owned M2
 * stacks, so an observed frame address cannot be trusted by itself. */
static bool within(uintptr_t bottom,size_t bytes,uintptr_t ptr,size_t need)
{
    return bottom<=UINTPTR_MAX-bytes && need!=0 &&
           bytes>=need && ptr>=bottom && ptr-bottom<=bytes-need;
}
static bool canonical(uint64_t value)
{
    return (value>>48)==(((value>>47)&1u)!=0 ?
        UINT64_C(0xffff):UINT64_C(0));
}

/* Index mapping is the fixed fixture topology, not a second task table. */
static unsigned index_of_slot(size_t slot)
{
    for(unsigned i=0;i<C2B_STACKS;++i)
        if(ids[i].generation!=0 && ids[i].slot==slot) return i;
    return C2B_STACKS;
}

/* Trust no supervisor saved pointer until exact-generation stack and
 * entire 20-qword IRQ return geometry/CS/SS/IF are revalidated. Unlike
 * ordinary tasks, reserved supervisor slot15 is excluded from D5.3. */
static bool supervisor_frame_safe(const uint64_t *frame,
    enum kernel_task_state required)
{
    const struct kernel_task_handle h=ids[C2B_SUPERVISOR];
    if(frame==NULL || !kernel_task_resources_check(&resources,&tasks) ||
       h.slot!=KERNEL_TASK_SUPERVISOR_SLOT ||
       tasks.tasks[h.slot].generation!=h.generation ||
       tasks.tasks[h.slot].state!=required) return false;
    const struct kernel_task_resource_record *r=&resources.task[h.slot];
    const uintptr_t ptr=(uintptr_t)frame,bottom=r->stack.bottom;
    const size_t bytes=M10_D2_FRAME_QWORDS*sizeof(uint64_t);
    if(!r->installed || r->quarantined ||
       r->handle.slot!=h.slot || r->handle.generation!=h.generation ||
       r->owned_locks!=0 || r->registered_waiters!=0 ||
       (ptr&7u)!=0 ||
       !within(bottom,r->stack.bytes,ptr,bytes)) return false;
    const uint64_t rip=frame[M10_D2_RIP],rsp=frame[M10_D2_RSP];
    return canonical(rip) && canonical(rsp) &&
       frame[M10_D2_CS]==expected_cs &&
       frame[M10_D2_SS]==expected_ss &&
       (frame[M10_D2_RFLAGS]&UINT64_C(0x202))==UINT64_C(0x202) &&
       within(bottom,r->stack.bytes,(uintptr_t)rsp,1) &&
       rsp>ptr && rsp-ptr>=bytes;
}

/* A worker's saved IRQ frame needs both the old C1 hardware-style frame
 * checks and complete D5.3 exact-generation owner/waiter invariants.
 * The high waiter has only a RET continuation and is ALWAYS excluded. */
static bool ordinary_frame_safe(unsigned index,const uint64_t *frame,
    enum kernel_task_state required)
{
    if(index!=C2B_LOW && index!=C2B_MEDIUM) return false;
    return d58c_irq_frame_eligible(&tasks,&resources,&sync,ids[index],
        required,frame,expected_cs,expected_ss,true,true,true);
}

/* The real x86-irq0.S frame is the ONLY accepted source. D5.7's policy
 * reads D5.3 effective donation but NEVER mutates a lock in IRQ0.
 * M10-A alone commits an exact-generation RUNNING/READY handoff. */
uint64_t *m10_d58c_c2b_real_irq_select(uint64_t *incoming)
{
    if(!active) return incoming;
    const unsigned from=index_of_slot(tasks.current_slot);
    uint64_t live_flags;
    __asm__ volatile("pushfq; popq %0":"=r"(live_flags)::"memory");
    if(from>=C2B_STACKS || from==C2B_HIGH ||
       (live_flags&UINT64_C(0x200))!=0 ||
       !d53_check(&sync,&resources,&tasks) ||
       !(from==C2B_SUPERVISOR ?
            supervisor_frame_safe(incoming,KERNEL_TASK_RUNNING):
            ordinary_frame_safe(from,incoming,KERNEL_TASK_RUNNING))) {
        failed=true;reason="UNSAFE_IRQ_SOURCE";active=false;
        return incoming;
    }
    if(!d57_guard_may_switch(&guard,true,
            (incoming[M10_D2_RFLAGS]&UINT64_C(0x200))!=0,true)) {
        failed=true;reason="IRQ_GUARD_DENIED";active=false;
        return incoming;
    }
    irq_frame[from]=incoming;
    if(ticks!=UINT32_MAX) ++ticks;
    if(slices[from]!=UINT32_MAX) ++slices[from];
    if(!d55c_tick_observe(&service,true,ticks)) {
        failed=true;reason="SUPERVISOR_DEADLINE";active=false;
        return incoming;
    }
    struct kernel_task_handle selected={0};
    if(!d57_select(&policy,&sync,&resources,&tasks,&selected)) {
        failed=true;reason="IRQ_PRIORITY_GRAPH";active=false;
        return incoming;
    }
    const unsigned to=index_of_slot(selected.slot);
#if defined(SPIFFOS_M10_D58C_C2B_BAD_TARGET_TEST)
    /* Test-only, nonmutating wrong-CS witness: deliberately refuse the
     * actual selected destination before M10-A changes any state.
     * Avoid corrupting the physical frame itself. */
    if(ticks==1 && from==C2B_LOW && to<C2B_STACKS &&
       to!=C2B_HIGH && irq_frame[to]!=NULL) {
        bool inadmissible=to==C2B_SUPERVISOR ?
            (supervisor_frame_safe(irq_frame[to],KERNEL_TASK_RUNNABLE) &&
             irq_frame[to][M10_D2_CS]!=
                (uint16_t)(expected_cs^UINT16_C(1))):
            !d58c_irq_frame_eligible(&tasks,&resources,&sync,ids[to],
                KERNEL_TASK_RUNNABLE,irq_frame[to],
                (uint16_t)(expected_cs^UINT16_C(1)),expected_ss,
                true,true,true);
        if(inadmissible) {
            negative_rejected=true;failed=true;
            reason="BAD_TARGET_FRAME_DENIED";active=false;
            return incoming;
        }
        failed=true;reason="BAD_TARGET_ACCEPTED";active=false;
        return incoming;
    }
#endif
#if defined(SPIFFOS_M10_D58C_C2B_BAD_GENERATION_TEST)
    /* Source frame and destination are both authenticated. Pass a stale
     * target identity to M10-A and verify refusal left task state intact. */
    if(ticks==1 && from==C2B_LOW && to<C2B_STACKS &&
       to!=C2B_HIGH && irq_frame[to]!=NULL) {
        struct kernel_task_handle stale=ids[to];
        ++stale.generation;
        const size_t before=tasks.current_slot,queued=tasks.ready_count;
        struct kernel_task_handle chosen={0};
        enum kernel_task_result result=kernel_task_irq_handoff(
            &tasks,ids[from],stale,&chosen);
        if(result==KERNEL_TASK_BAD_HANDLE &&
           tasks.current_slot==before && tasks.ready_count==queued) {
            negative_rejected=true;failed=true;
            reason="STALE_TARGET_DENIED";active=false;
            return incoming;
        }
        failed=true;reason="STALE_TARGET_MUTATED";active=false;
        return incoming;
    }
#endif
    if(to>=C2B_STACKS || to==C2B_HIGH || to==from ||
       irq_frame[to]==NULL ||
       !(to==C2B_SUPERVISOR ?
           supervisor_frame_safe(irq_frame[to],KERNEL_TASK_RUNNABLE):
           ordinary_frame_safe(to,irq_frame[to],KERNEL_TASK_RUNNABLE))) {
        failed=true;reason="UNSAFE_IRQ_TARGET";active=false;
        return incoming;
    }
    /* C2b mode admission rejects any READY cooperative RET target. The
     * supervisor has its own separate reserved-frame verifier above. */
    if(to!=C2B_SUPERVISOR && from!=C2B_SUPERVISOR) {
        struct d58c_context_view target={
            .handle=ids[to],.form=D58C_CONTEXT_VERIFIED_IRQ_IRETQ,
            .saved_irq_frame=irq_frame[to]
        };
        if(!d58c_c2b_irq_target_eligible(&tasks,&resources,&sync,
               ids[from],incoming,&target,expected_cs,expected_ss,
               true,true,true)) {
            failed=true;reason="CONTEXT_FORM_REJECTED";active=false;
            return incoming;
        }
    }
    enum kernel_priority_class effective=KERNEL_PRIORITY_BACKGROUND;
    if(from==C2B_MEDIUM && to==C2B_LOW &&
       d53_effective(&sync,&resources,&tasks,ids[C2B_LOW],&effective)==D53_OK &&
       effective==KERNEL_PRIORITY_TIME_SENSITIVE &&
       donated_medium_to_low!=UINT32_MAX)
        ++donated_medium_to_low;
    struct kernel_task_handle chosen={0};
    if(kernel_task_irq_handoff(&tasks,ids[from],selected,&chosen)!=
           KERNEL_TASK_OK ||
       chosen.slot!=selected.slot ||
       chosen.generation!=selected.generation) {
        failed=true;reason="M10_A_IRQ_HANDOFF";active=false;
        return incoming;
    }
    if(!d57_note_handoff(&policy,&tasks,chosen)) {
        failed=true;reason="POLICY_AFTER_HANDOFF";
        /* M10-A already committed chosen. Returning its verified frame
         * avoids resuming the source with a conflicting RUNNING identity. */
    }
    if(irq_switches!=UINT32_MAX) ++irq_switches;
    return irq_frame[to];
}

/* Medium is an independently owned, initial full-IRETQ worker. It never
 * invokes any D5.3 mutator or cooperative RET switch. */
__attribute__((noreturn)) static void medium_cpu(void)
{
    volatile uint64_t work=3;
    while(active && !failed) {
        work=work*UINT64_C(6364136223846793005)+1u;
        ++medium_work;
        __asm__ volatile("pause":::"memory");
    }
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}

/* Reserved M10-A supervisor has its OWN actual CPU stack and a per-tick
 * completed-work witness; scheduling alone never resets service budget. */
__attribute__((noreturn)) static void supervisor_cpu(void)
{
    volatile uint64_t work=7;
    while(active && !failed) {
        work=work*UINT64_C(6364136223846793005)+1u;
        ++supervisor_work;
        if(!d55c_tick_service_completed(&service,true,true,ticks)) {
            failed=true;reason="SUPERVISOR_SERVICE";
        }
        __asm__ volatile("pause":::"memory");
    }
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}

/* Construct the exact D2 20-qword initial IRETQ layout on a separately
 * owned, aligned M2 stack, matching D5.6's established worker frame. This
 * is NOT an arbitrary RET stack conversion: neither medium nor supervisor
 * has ever executed in a cooperative context. Refuse invalid dimensions
 * before writing a single byte. Caller owns exact stack lifetime. */
static bool prepare_irq_frame(unsigned index,void (*entry)(void))
{
    if(index!=C2B_MEDIUM && index!=C2B_SUPERVISOR) return false;
    const uintptr_t base=(uintptr_t)stacks[index];
    const size_t bytes=M10_D2_FRAME_QWORDS*sizeof(uint64_t);
    if(base==0 || entry==NULL || (base&15u)!=0 ||
       base>UINTPTR_MAX-X86_TASK_STACK_BYTES ||
       X86_TASK_STACK_BYTES<bytes+8u) return false;
    const uintptr_t top=base+X86_TASK_STACK_BYTES;
    const uintptr_t rsp=top-8u;
    uint64_t *frame=(uint64_t *)(rsp-bytes);
    for(size_t i=0;i<M10_D2_FRAME_QWORDS;++i) frame[i]=0;
    *(uint64_t *)rsp=0;
    frame[M10_D2_RIP]=(uintptr_t)entry;
    frame[M10_D2_CS]=expected_cs;
    frame[M10_D2_RFLAGS]=UINT64_C(0x202);
    frame[M10_D2_RSP]=rsp;
    frame[M10_D2_SS]=expected_ss;
    irq_frame[index]=frame;
    return true;
}

/* Owner starts via a genuine cooperative stack, acquires rank 0 and
 * becomes BLOCKED waiting for the external event. D5.8b's IF-off switch
 * preserves its original saved RET continuation until the boot dispatcher
 * wakes this exact generation and actually resumes its x86 context. */
__attribute__((noreturn)) static void low_owner(void)
{
    uint64_t flags=irq_off();
    if(d53_acquire(&sync,&resources,&tasks,0,ids[C2B_LOW])!=D53_OK ||
       kernel_task_block(&tasks,ids[C2B_LOW])!=KERNEL_TASK_OK) {
        failed=true;reason="LOW_INIT_BLOCK";
    }
    x86_d58b_block_switch(&cooperative[C2B_LOW],&boot);
    irq_restore(flags);

    /* Bootstrap has registered high as genuinely BLOCKED on this owner.
     * Arm only after validating that medium/supervisor have complete,
     * independently bound IRETQ frames and no waiter is a READY IRQ target. */
    flags=irq_off();
    enum kernel_priority_class donated=KERNEL_PRIORITY_BACKGROUND;
    bool ready=tasks.current_slot==ids[C2B_LOW].slot &&
        tasks.tasks[ids[C2B_LOW].slot].state==KERNEL_TASK_RUNNING &&
        tasks.tasks[ids[C2B_HIGH].slot].state==KERNEL_TASK_BLOCKED &&
        resources.task[ids[C2B_HIGH].slot].registered_waiters==1 &&
        tasks.tasks[ids[C2B_MEDIUM].slot].state==KERNEL_TASK_RUNNABLE &&
        tasks.tasks[ids[C2B_SUPERVISOR].slot].state==KERNEL_TASK_RUNNABLE &&
        d53_effective(&sync,&resources,&tasks,ids[C2B_LOW],&donated)==D53_OK &&
        donated==KERNEL_PRIORITY_TIME_SENSITIVE &&
        ordinary_frame_safe(C2B_MEDIUM,irq_frame[C2B_MEDIUM],
                            KERNEL_TASK_RUNNABLE) &&
        supervisor_frame_safe(irq_frame[C2B_SUPERVISOR],
                              KERNEL_TASK_RUNNABLE) &&
        d53_check(&sync,&resources,&tasks);
    if(!ready) {failed=true;reason="LOW_ARM_VALIDATION";}
    else {
#if defined(SPIFFOS_M10_D58C_C2B_BAD_SOURCE_TEST)
        /* Fault the expected segment witness from this valid owner-owned
         * non-IRQ IF=0 safe point, never fabricate or mutate CPU frames. */
        expected_cs=(uint16_t)(expected_cs^UINT16_C(1));
#endif
#if defined(SPIFFOS_M10_D58C_C2B_BAD_GRAPH_TEST)
        /* Corrupt only diagnostic D5.3 profile metadata in non-IRQ IF=0,
         * so first authentic IRQ0 must refuse the entire generation graph. */
        ++sync.profiles[ids[C2B_LOW].slot].handle.generation;
#endif
#if defined(SPIFFOS_M10_D58C_C2B_BAD_GUARD_TEST)
        /* Deliberately leave diagnostic guard depth nonzero at tick entry:
         * no IRQ handoff may occur regardless of otherwise valid frames. */
        guard.depth=1;
#endif
        active=true;
    }
    irq_restore(flags);

    /* The real IRQ0 hardware independently builds the owner's frame after
     * its first tick and is permitted to switch M10-A to the medium or
     * reserved supervisor only after validation. */
    while(active && !failed && ticks<C2B_STOP_TICKS)
        __asm__ volatile("pause":::"memory");

    /* This worker is running ONLY when M10-A has actually handed it the
     * CPU via a complete saved IRETQ frame. Disarm under IF=0 before the
     * lock release can wake a RET-only waiter into the ready queue. */
    flags=irq_off();
    active=false;
    if(!failed && (tasks.current_slot!=ids[C2B_LOW].slot ||
       ticks<C2B_STOP_TICKS ||
       donated_medium_to_low<C2B_MIN_SLICES ||
       slices[C2B_LOW]<C2B_MIN_SLICES ||
       slices[C2B_MEDIUM]<C2B_MIN_SLICES ||
       slices[C2B_SUPERVISOR]<C2B_MIN_SLICES ||
       medium_work==0 || supervisor_work==0 ||
       service.service_completions<C2B_MIN_SLICES ||
       service.max_observed_gap_ticks>C2B_MAX_SERVICE_GAP)) {
        failed=true;reason="IRQ_METRICS";
    }
    if(!failed && d53_release(&sync,&resources,&tasks,0,
                               ids[C2B_LOW])!=D53_OK) {
        failed=true;reason="LOW_RELEASE";
    }
    if(!failed && (tasks.tasks[ids[C2B_HIGH].slot].state!=
                       KERNEL_TASK_RUNNABLE ||
                   resources.task[ids[C2B_HIGH].slot].owned_locks!=1 ||
                   resources.task[ids[C2B_HIGH].slot].registered_waiters!=0)) {
        failed=true;reason="HIGH_NOT_WOKEN";
    }
    if(!failed && kernel_task_yield(&tasks,ids[C2B_LOW])!=KERNEL_TASK_OK) {
        failed=true;reason="LOW_YIELD";
    }
    phase_finished=true;
    /* Actual x86 cooperative continuation saved on THIS worker stack:
     * it is not synthesized from the current 20-qword IRQ frame. CPU IF
     * stays disabled until the permanent boot dispatcher resumes safely. */
    x86_d58b_block_switch(&cooperative[C2B_LOW],&boot);
    irq_restore(flags);
    if(!failed) {
        const uint64_t final=irq_off();
        if(kernel_task_terminate(&tasks,ids[C2B_LOW])!=KERNEL_TASK_OK)
            {failed=true;reason="LOW_EXIT";}
        low_done=true;
        x86_d58b_block_switch(&cooperative[C2B_LOW],&boot);
        irq_restore(final);
    }
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}

/* A real high-priority D5.3 waiter is BLOCKED in M10-A and keeps its own
 * cooperative RET stack, NOT an IRETQ frame. Its next instruction after
 * x86_d58b_block_switch is unreachable until off-IRQ owner release/wake
 * and the trusted boot dispatcher resumes the exact old CPU context. */
__attribute__((noreturn)) static void high_waiter(void)
{
    uint64_t flags=irq_off();
    if(d53_acquire(&sync,&resources,&tasks,0,ids[C2B_HIGH])!=D53_BLOCKED) {
        failed=true;reason="HIGH_NOT_BLOCKED";
    }
    if(!failed && !d58b_blocked_departure_metadata(&tasks,&resources,&sync,
                                                     ids[C2B_HIGH],false,true)) {
        failed=true;reason="HIGH_BLOCK_METADATA";
    }
    x86_d58b_block_switch(&cooperative[C2B_HIGH],&boot);
    irq_restore(flags);

    flags=irq_off();
    if(!phase_finished || active || failed ||
       tasks.current_slot!=ids[C2B_HIGH].slot ||
       resources.task[ids[C2B_HIGH].slot].owned_locks!=1 ||
       d53_release(&sync,&resources,&tasks,0,ids[C2B_HIGH])!=D53_OK) {
        failed=true;reason="HIGH_WAKE_RELEASE";
    }
    if(!failed && kernel_task_terminate(&tasks,ids[C2B_HIGH])!=KERNEL_TASK_OK) {
        failed=true;reason="HIGH_EXIT";
    }
    high_done=true;
    x86_d58b_block_switch(&cooperative[C2B_HIGH],&boot);
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}

/* Validate that the old worker's actual CPU RSP was left behind before
 * bootstrap touches a task ledger or selects another stack. This is
 * physical provenance, not an inference from M10-A metadata alone. */
static bool boot_off_worker_stacks(void)
{
    uintptr_t actual;
    __asm__ volatile("movq %%rsp,%0":"=r"(actual)::"memory");
    for(unsigned i=0;i<C2B_STACKS;++i)
        if(stacks[i]!=NULL &&
           within((uintptr_t)stacks[i],X86_TASK_STACK_BYTES,actual,1))
            return false;
    return true;
}

/* Bootstrap starts the two cooperative workers, then transfers the RUNNING
 * owner to a real IRQ0-scheduled phase. The high waiter never appears as a
 * READY IRETQ candidate and may only be resumed cooperatively after IRQ
 * disarm, true off-IRQ lock release, and M10-A wake. */
bool m10_d58c_c2b_real_guest_smoke(bool (*clock_ms)(uint64_t *))
{
    if(clock_ms==NULL) {reason="CLOCK";return false;}
    uint64_t flags=irq_off();
    if((flags&UINT64_C(0x200))==0) {
        reason="BOOT_IF_DISABLED";return false;
    }
    unsigned prepared=0;
    for(;prepared<C2B_STACKS;++prepared) {
        void *owned=NULL;
        if(!memory_boot_m10_task_stack_prepare(prepared,&owned)) break;
        stacks[prepared]=(uint8_t *)owned;
    }
    irq_restore(flags);
    if(prepared!=C2B_STACKS) {reason="STACK_PREPARE";return false;}

    if(!kernel_task_init(&tasks) ||
       !kernel_task_resources_init(&resources) ||
       !d53_init(&sync) || !d57_policy_init(&policy) ||
       !d57_guard_init(&guard) ||
       !d55c_tick_init(&service,0,C2B_MAX_SERVICE_GAP)) {
        reason="INIT";return false;
    }
    __asm__ volatile("movw %%cs,%0":"=r"(expected_cs));
    __asm__ volatile("movw %%ss,%0":"=r"(expected_ss));
    for(unsigned i=0;i<C2B_STACKS;++i)
        for(size_t j=0;j<X86_TASK_STACK_BYTES;++j)
            stacks[i][j]=UINT8_C(0xa7);
    if(!x86_task_context_init(&cooperative[C2B_LOW],
              stacks[C2B_LOW],X86_TASK_STACK_BYTES,low_owner) ||
       !x86_task_context_init(&cooperative[C2B_HIGH],
              stacks[C2B_HIGH],X86_TASK_STACK_BYTES,high_waiter) ||
       !prepare_irq_frame(C2B_MEDIUM,medium_cpu) ||
       !prepare_irq_frame(C2B_SUPERVISOR,supervisor_cpu)) {
        reason="CONTEXT_INIT";return false;
    }
    flags=irq_off();
    bool valid=true;
    /* M10-A gives the supervisor its reserved slot15, not an ordinary
     * lock participant. The M2-backed stacks are never shared. */
    for(unsigned i=0;i<C2B_STACKS && valid;++i) {
        const bool reserved=i==C2B_SUPERVISOR;
        const struct kernel_task_stack_binding binding={
            .bottom=(uintptr_t)stacks[i],.bytes=X86_TASK_STACK_BYTES,
            .saved_rsp=i<=C2B_HIGH ?
                 (uintptr_t)cooperative[i].saved_rsp:
                 (uintptr_t)irq_frame[i],
            .owner_index=i
        };
        const enum kernel_priority_class priority=i==C2B_LOW ?
            KERNEL_PRIORITY_BACKGROUND :
            i==C2B_HIGH? KERNEL_PRIORITY_TIME_SENSITIVE:
            KERNEL_PRIORITY_INTERACTIVE;
        valid=kernel_task_create(&tasks,reserved,&ids[i])==KERNEL_TASK_OK &&
              kernel_task_resources_bind(&resources,&tasks,
                  ids[i],&binding)==KERNEL_RESOURCE_OK &&
              (reserved || d53_enroll(&sync,&resources,&tasks,
                     ids[i],priority)==D53_OK);
    }
    if(valid) valid=kernel_task_make_runnable(&tasks,ids[C2B_LOW])==
                   KERNEL_TASK_OK;
    struct kernel_task_handle selected={0};
    if(valid) valid=kernel_task_dispatch_selected(&tasks,ids[C2B_LOW],
                 &selected)==KERNEL_TASK_OK;
    irq_restore(flags);
    if(!valid) {reason="CREATE_BIND";return false;}

    /* Low first acquires rank 0, becomes BLOCKED and yields the CPU with
     * IF=0 to this unchanged boot stack. The owner backing stays live. */
    x86_task_switch(&boot,&cooperative[C2B_LOW]);
    uint64_t live_if;
    __asm__ volatile("pushfq; popq %0":"=r"(live_if)::"memory");
    if((live_if&UINT64_C(0x200))!=0 ||
       !boot_off_worker_stacks() ||
       tasks.current_slot!=SIZE_MAX ||
       tasks.tasks[ids[C2B_LOW].slot].state!=KERNEL_TASK_BLOCKED ||
       resources.task[ids[C2B_LOW].slot].owned_locks!=1 ||
       !d53_check(&sync,&resources,&tasks)) {
        reason="LOW_BLOCK_STACK";return false;
    }
    irq_restore(flags);

    /* High next becomes genuinely BLOCKED waiting on rank0 and preserves
     * exactly one D5.3 waiter ref on its separate physical RET stack. */
    flags=irq_off();
    valid=kernel_task_make_runnable(&tasks,ids[C2B_HIGH])==KERNEL_TASK_OK &&
          kernel_task_dispatch_selected(&tasks,ids[C2B_HIGH],
              &selected)==KERNEL_TASK_OK;
    irq_restore(flags);
    if(!valid) {reason="HIGH_DISPATCH";return false;}
    x86_task_switch(&boot,&cooperative[C2B_HIGH]);
    __asm__ volatile("pushfq; popq %0":"=r"(live_if)::"memory");
    if((live_if&UINT64_C(0x200))!=0 ||
       !boot_off_worker_stacks() ||
       !d58b_blocked_departure_metadata(&tasks,&resources,
                                          &sync,ids[C2B_HIGH],false,true)) {
        reason="HIGH_BLOCK_STACK";return false;
    }
    irq_restore(flags);

    /* Medium and reserved supervisor are fully initialized as IRETQ
     * contexts BEFORE becoming READY. Owner's external event is now
     * complete, while high must remain BLOCKED and unresumable in IRQ. */
    flags=irq_off();
    valid=kernel_task_make_runnable(&tasks,ids[C2B_MEDIUM])==KERNEL_TASK_OK &&
          kernel_task_make_runnable(&tasks,
                                    ids[C2B_SUPERVISOR])==KERNEL_TASK_OK &&
          kernel_task_wake(&tasks,ids[C2B_LOW])==KERNEL_TASK_OK &&
          kernel_task_dispatch_selected(&tasks,ids[C2B_LOW],
              &selected)==KERNEL_TASK_OK &&
          d53_check(&sync,&resources,&tasks);
    irq_restore(flags);
    if(!valid) {reason="IRQ_PHASE_PREPARE";return false;}

    /* The owner resumes through its previously saved cooperative RET
     * stack. Genuine timer IRQ then creates a new full hardware frame,
     * which the verified IRQ selector may save and hand off via M10-A. */
    x86_task_switch(&boot,&cooperative[C2B_LOW]);
    __asm__ volatile("pushfq; popq %0":"=r"(live_if)::"memory");
    if((live_if&UINT64_C(0x200))!=0 || !boot_off_worker_stacks() ||
       active || !phase_finished || failed ||
       tasks.current_slot!=SIZE_MAX || !d53_check(&sync,&resources,&tasks) ||
       resources.task[ids[C2B_HIGH].slot].owned_locks!=1 ||
       tasks.tasks[ids[C2B_HIGH].slot].state!=KERNEL_TASK_RUNNABLE) {
        reason=failed?reason:"IRQ_OFFRAMP";
        return false;
    }
    irq_restore(flags);

    /* Both medium and supervisor are READY IRETQ-only tasks, not RET
     * continuations; terminate them from surviving dispatcher while IF=0,
     * then preserve their physical stacks until safe M2 reaper cleanup. */
    flags=irq_off();
    valid=kernel_task_terminate(&tasks,ids[C2B_MEDIUM])==KERNEL_TASK_OK &&
          kernel_task_terminate(&tasks,
                                 ids[C2B_SUPERVISOR])==KERNEL_TASK_OK &&
          kernel_task_dispatch_selected(&tasks,ids[C2B_HIGH],
              &selected)==KERNEL_TASK_OK;
    irq_restore(flags);
    if(!valid) {reason="POST_IRQ_DISPATCH";return false;}

    x86_task_switch(&boot,&cooperative[C2B_HIGH]);
    __asm__ volatile("pushfq; popq %0":"=r"(live_if)::"memory");
    if((live_if&UINT64_C(0x200))!=0 || !boot_off_worker_stacks() ||
       !high_done || failed || tasks.current_slot!=SIZE_MAX ||
       resources.task[ids[C2B_HIGH].slot].owned_locks!=0) {
        reason=failed?reason:"HIGH_OFFSTACK";
        return false;
    }
    irq_restore(flags);

    flags=irq_off();
    valid=kernel_task_dispatch_selected(&tasks,ids[C2B_LOW],
            &selected)==KERNEL_TASK_OK;
    irq_restore(flags);
    if(!valid) {reason="LOW_FINAL_DISPATCH";return false;}
    x86_task_switch(&boot,&cooperative[C2B_LOW]);
    __asm__ volatile("pushfq; popq %0":"=r"(live_if)::"memory");
    if((live_if&UINT64_C(0x200))!=0 || !boot_off_worker_stacks() ||
       !low_done || failed || tasks.current_slot!=SIZE_MAX) {
        reason=failed?reason:"LOW_OFFSTACK";
        return false;
    }
    irq_restore(flags);

    /* Physical saved frames are now inactive; no lock/waiter refs exist,
     * all M10-A records are TERMINATED, and bootstrap runs independently.
     * Only now can the ordinary M2 resource-owner reaper release backing. */
    flags=irq_off();
    uintptr_t boot_rsp;
    __asm__ volatile("movq %%rsp,%0":"=r"(boot_rsp)::"memory");
    bool good=kernel_task_check(&tasks) &&
        d53_check(&sync,&resources,&tasks) &&
        ticks>=C2B_STOP_TICKS &&
        donated_medium_to_low>=C2B_MIN_SLICES &&
        irq_switches>=C2B_MIN_SLICES &&
        service.service_completions>=C2B_MIN_SLICES &&
        service.max_observed_gap_ticks<=C2B_MAX_SERVICE_GAP;
    for(unsigned i=0;i<C2B_STACKS;++i) {
        if(tasks.tasks[ids[i].slot].state!=KERNEL_TASK_TERMINATED ||
           resources.task[ids[i].slot].owned_locks!=0 ||
           resources.task[ids[i].slot].registered_waiters!=0) good=false;
        for(size_t j=0;j<C2B_CANARY_BYTES;++j)
            if(stacks[i][j]!=UINT8_C(0xa7)) good=false;
    }
    for(unsigned i=0;i<C2B_STACKS && good;++i) {
        /* Last saved context is either the true saved six GPR RET frame
         * after off-ramp or the latest actual IRQ0 frame on that owner's
         * physical backing. Source is not a caller-fabricated pointer. */
        uintptr_t saved=i<=C2B_HIGH ?
            (uintptr_t)cooperative[i].saved_rsp:(uintptr_t)irq_frame[i];
        if(kernel_task_resources_saved_rsp(&resources,&tasks,ids[i],
              saved)!=KERNEL_RESOURCE_OK) good=false;
        if(good && kernel_task_resources_retire(&resources,&tasks,ids[i],
              boot_rsp,memory_boot_m10_task_stack_release)!=
                KERNEL_RESOURCE_OK) good=false;
    }
    irq_restore(flags);
    if(!good) {reason="FINAL_OWNERSHIP";return false;}
    return true;
}

/* Serial reporting occurs only on the permanent boot stack, after all
 * four worker-owned backing allocations are fully detached and reclaimed. */
static void report_number(bool (*write)(const char *),uint64_t value)
{
    char digits[21];
    unsigned pos=20;
    digits[pos]='\0';
    do {
        digits[--pos]=(char)('0'+value%10u);
        value/=10u;
    } while(value!=0);
    (void)write(&digits[pos]);
}
void m10_d58c_c2b_real_report(bool (*write)(const char *))
{
    if(write==NULL || !phase_finished || !low_done || !high_done || failed)
        return;
    (void)write("C2B_IRQ_LOW_SLICES:");
    report_number(write,slices[C2B_LOW]);
    (void)write("\r\nC2B_IRQ_MEDIUM_SLICES:");
    report_number(write,slices[C2B_MEDIUM]);
    (void)write("\r\nC2B_IRQ_SUPERVISOR_SLICES:");
    report_number(write,slices[C2B_SUPERVISOR]);
    (void)write("\r\nC2B_MEDIUM_TO_DONATED_LOW:");
    report_number(write,donated_medium_to_low);
    (void)write("\r\nC2B_IRQ_SWITCHES:");
    report_number(write,irq_switches);
    (void)write("\r\nC2B_COMPLETED_SUPERVISOR_SERVICE:");
    report_number(write,service.service_completions);
    (void)write("\r\nC2B_MAX_SUPERVISOR_GAP_TICKS:");
    report_number(write,service.max_observed_gap_ticks);
    (void)write("\r\nC2B_MEDIUM_CPU_ITERATIONS:");
    report_number(write,medium_work);
    (void)write("\r\nC2B_SUPERVISOR_CPU_ITERATIONS:");
    report_number(write,supervisor_work);
    (void)write("\r\n");
}
#endif
