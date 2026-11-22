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
#ifdef SPIFFOS_M10_D59_PREVIEW
#include "m10-d59-preview.h"
#endif
#ifdef SPIFFOS_M10_D57_IRQ_TEST
#include "m10-d57-safety.h"
#endif
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
#include "m10-d58-notify.h"
#endif
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
#include "m10-d58c-irq-frame.h"
#endif
#ifdef SPIFFOS_M10_D59B_CONTEXT_TEST
#include "m10-d59b-context.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef SPIFFOS_M10_D56_IRQ_TEST
enum { D56_WORKERS=2,
#ifdef SPIFFOS_M10_D59_PREVIEW
       D56_FG_INDEX=2, D56_STACKS=4, D56_SUP_INDEX=3,
#else
       D56_STACKS=3, D56_SUP_INDEX=2,
#endif
       D56_CANARY=64,
#ifdef SPIFFOS_M10_D59_PREVIEW_BOUNDED_TEST
       D56_STOP_TICKS=320,
#elif defined(SPIFFOS_M10_D57_IRQ_TEST)
       D56_STOP_TICKS=220,
#else
       D56_STOP_TICKS=120,
#endif
       D56_MAX_SERVICE_GAP=8, D56_MIN_WORKER_TICKS=12 };

static struct kernel_task_table tasks;
static struct kernel_task_resources owners;
static struct kernel_task_handle handles[D56_STACKS];
static struct x86_task_context supervisor_context,boot_context;
static uint8_t *stacks[D56_STACKS];
static uint64_t *saved_frames[D56_STACKS];
static uint64_t expected_cs,expected_ss;
static struct d55c_tick_service service;
#ifdef SPIFFOS_M10_D57_IRQ_TEST
/* The D5.3 graph is a bounded read-only IRQ input, never a second
 * scheduler or mutex mutator. Guard cookies are owned by current CPU. */
static struct d53_sync d57_sync;
static struct d57_irq_policy d57_policy;
static struct d57_preempt_guard d57_guard;
static uint32_t d57_guard_cookies[D57_MAX_GUARD_DEPTH];
#ifdef SPIFFOS_M10_D59B_CONTEXT_TEST
static struct d59b_context d59b_context;
static volatile uint32_t d59b_cycles[D56_WORKERS], d59b_real_irq_entries;
static uint64_t d59b_max_body_cycles;
#ifdef SPIFFOS_M10_D59B_PAGE_CLAIM_TEST
static volatile uint32_t d59b_page_cycles[D56_WORKERS];
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
static volatile uint32_t d59b_direct_cycles[D56_WORKERS];
#endif
#endif
#ifdef SPIFFOS_M10_D59B_SERVICE_TEST
static volatile uint32_t d59b_service_cycles[D56_WORKERS];
#endif
/* LFENCE/RDTSC samples guest TSC cycles, not calibrated wall-clock time.
 * It measures the held transaction body; adapter entry/exit is excluded. */
static uint64_t d59b_tsc(void)
{
    uint32_t low,high;
    __asm__ volatile("lfence; rdtsc":"=a"(low),"=d"(high)::"memory");
    return ((uint64_t)high<<32)|low;
}
#endif
#endif
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
/* Test-only IRQ observation transport; never a lock mutation queue. */
static struct d58_notice_queue d58_observations;
static uint32_t d58_drained;
#endif
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
/* C1: count actual IRQ0 interruptions on a D5.3 registered live owner.
 * These are measurements, never an alternate scheduler authority. */
static volatile uint32_t d58c_locked_source_irqs;
static volatile uint32_t d58c_lock_acquires,d58c_lock_releases;
#endif
static volatile uint64_t iterations[D56_WORKERS];
static volatile uint32_t slices[D56_STACKS],ticks,irq_switches;
static volatile uint32_t critical_depth,critical_cycles;
static volatile bool active,done,failed,returned_to_boot;
static bool negative_rejected;
static const char *reason="UNSET";
/* Static, nonallocating mapping needed by guard before function definition. */
static unsigned index_of_slot(size_t slot);

#ifdef SPIFFOS_M10_D59_PREVIEW
/* Copy one coherent scheduler sample into the foreground caller's output.
 * Tick and service fields are counts, and worker iterations are unitless.
 * The borrowed output changes only after successful exact-generation guard
 * release. This runs off IRQ, allocates nothing and preserves caller IF.
 * An uncertain guard exit stops while still on the current stack. */
bool m10_d59_preview_snapshot(struct m10_d59_preview_metrics *out)
{
    uint32_t cookie = 0;
    if (out == NULL || !d59b_enter(&d59b_context, &cookie)) return false;
    const bool foreground = active && !failed &&
        tasks.current_slot == handles[D56_FG_INDEX].slot;
    const struct m10_d59_preview_metrics metrics = {
        .ticks = ticks,
        .worker_ticks = {slices[0], slices[1]},
        .foreground_ticks = slices[D56_FG_INDEX],
        .irq_switches = irq_switches,
        .supervisor_completions = service.service_completions,
        .max_supervisor_gap = service.max_observed_gap_ticks,
        .worker_iterations = {iterations[0], iterations[1]}
    };
    if (!d59b_exit(&d59b_context, cookie)) d59b_memory_stop();
    if (!foreground) return false;
    *out = metrics;
    return true;
}
#endif

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
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    if(active) {
        const unsigned current=index_of_slot(tasks.current_slot);
        uint32_t cookie=0;
        if(current>=D56_STACKS ||
           tasks.tasks[handles[current].slot].generation!=
               handles[current].generation ||
           tasks.tasks[handles[current].slot].state!=KERNEL_TASK_RUNNING ||
           !d57_guard_enter(&d57_guard,handles[current],false,
               true,&cookie) ||
           d57_guard.depth==0 || d57_guard.depth>D57_MAX_GUARD_DEPTH) {
            failed=true;reason="GUARD_ENTER";
        } else d57_guard_cookies[d57_guard.depth-1]=cookie;
    }
#endif
    return flags;
}

/* LIFO matched critical exit, with fail-closed underflow detection. */
static void exit_critical(uint64_t previous_flags)
{
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    if(active) {
        /* Verify the owner against the authoritative M10-A record, not
         * just the guard's own stored identity. The one legal exception is
         * the terminating reserved supervisor: M10-A clears current_slot
         * on exit, while this IF=0 critical region still needs its final
         * LIFO pop before returning to the boot stack. */
        struct kernel_task_handle owner={0};
        if(tasks.current_slot!=SIZE_MAX) {
            const unsigned current=index_of_slot(tasks.current_slot);
            if(current<D56_STACKS &&
               tasks.tasks[handles[current].slot].state==
                   KERNEL_TASK_RUNNING &&
               tasks.tasks[handles[current].slot].generation==
                   handles[current].generation)
                owner=handles[current];
        } else if(d57_guard.owner.slot==handles[D56_SUP_INDEX].slot &&
                  d57_guard.owner.generation==
                      handles[D56_SUP_INDEX].generation &&
                  tasks.tasks[handles[D56_SUP_INDEX].slot].state==
                      KERNEL_TASK_TERMINATED)
            owner=handles[D56_SUP_INDEX];
        if(d57_guard.depth==0 ||
           !d57_guard_exit(&d57_guard,owner,false,true,
               d57_guard_cookies[d57_guard.depth-1])) {
            failed=true;reason="GUARD_EXIT";
        }
    }
#endif
    if(critical_depth==0) failed=true;
    else --critical_depth;
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    /* On supervisor termination M10-A has no running task. Disarm the
     * IRQ selector BEFORE restoring IF, eliminating a dangling-owner
     * window while cooperative switch returns to the boot dispatcher. */
    if(active && tasks.current_slot==SIZE_MAX &&
       d57_guard.depth==0) active=false;
#endif
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
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
    /* Diagnostic C1 *additional* whole-graph + frame proof, used only
     * for ordinary worker tasks. The supervisor must still pass the
     * original zero-lock gate below. No fake IRETQ or IRQ lock action. */
    if(index<D56_WORKERS) {
        uint64_t observed_flags;
        __asm__ volatile("pushfq; popq %0":"=r"(observed_flags)::"memory");
        return d58c_irq_frame_eligible(&tasks,&owners,&d57_sync,h,
            required,frame,(uint16_t)expected_cs,(uint16_t)expected_ss,
            true,(observed_flags&UINT64_C(0x200))==0,
            critical_depth==0 && d57_guard.depth==0);
    }
#endif
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
#ifdef SPIFFOS_M10_D59_PREVIEW
    if(slot==handles[D56_FG_INDEX].slot) return D56_FG_INDEX;
#endif
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
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
    /* C1 source is hardware-supplied via IRQ0 assembly and was already
     * checked with the full D5.8c frame/graph admission above. The older
     * D5.5c helper excludes all locks by design; do not weaken it. */
    return true;
#endif
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
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
    if(!d59b_memory_is_bound(&d59b_context)) {
        failed=true;reason="D59B_MEMORY_UNBOUND";active=false;return incoming;
    }
#endif
#ifdef SPIFFOS_M10_D59B_CONTEXT_TEST
    /* The word was incremented by actual assembly entry, not C caller
     * supplied evidence. Never allocate or release a physical frame here. */
    if(d59b_irq_depth!=1) {
        failed=true;reason="D59B_IRQ_DEPTH";active=false;return incoming;
    }
    ++d59b_real_irq_entries;
#ifdef SPIFFOS_M10_D59B_BAD_IRQ_TEST
    uint32_t rejected_cookie=UINT32_C(0xabc);
    if(d59b_enter(&d59b_context,&rejected_cookie) ||
       rejected_cookie!=UINT32_C(0xabc) || !d57_guard.faulted) {
        failed=true;reason="D59B_IRQ_ADMITTED";active=false;return incoming;
    }
    /* Deliberate rejected admission occurred before allocator work. The
     * supervisor is the initial task; disarm without switching stacks. */
    failed=true;reason="D59B_IRQ_DENIED";active=false;return incoming;
#endif
#endif
    const unsigned from=index_of_slot(tasks.current_slot);
    if(from>=D56_STACKS || critical_depth!=0 ||
       !incoming_safe(from,incoming)) {
        failed=true;reason="UNSAFE_CURRENT_FRAME";active=false;
        /* Unknown source is never used for a speculative task handoff. */
#ifdef SPIFFOS_M10_D59_PREVIEW
        d59b_memory_stop();
#else
        return incoming;
#endif
    }
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    /* Trusted RFLAGS comes from a frame that incoming_safe already
     * validated on this exact M10-A generation-owned stack. */
    uint64_t live_flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(live_flags)::"memory");
    if(!d57_guard_may_switch(&d57_guard,true,
        (incoming[M10_D2_RFLAGS]&UINT64_C(0x200))!=0,
        (live_flags&UINT64_C(0x200))==0)) {
        failed=true;reason="GUARD_IRQ_DENIED";active=false;
        return incoming;
    }
#endif
    saved_frames[from]=incoming;
    if(ticks!=UINT32_MAX) ++ticks;
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
    /* Independently sample live IF on genuine IRQ0 entry, instead of
     * supplying a reassuring constant to the diagnostic admission gate. */
    uint64_t d58_live_flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(d58_live_flags)::"memory");
    if(!d58_note_irq(&d58_observations,&tasks,handles[from],ticks,
                     true,(d58_live_flags&UINT64_C(0x200))==0)) {
        failed=true;reason="D58_IRQ_NOTICE";
    }
#endif
    if(slices[from]!=UINT32_MAX) ++slices[from];
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
    if(from==0 && owners.task[handles[0].slot].owned_locks!=0 &&
       d58c_locked_source_irqs!=UINT32_MAX)
        ++d58c_locked_source_irqs;
#endif
    if(!d55c_tick_observe(&service,true,ticks))
        {failed=true;reason="SUPERVISOR_DEADLINE";}
#if !defined(SPIFFOS_M10_D59_PREVIEW) || defined(SPIFFOS_M10_D59_PREVIEW_BOUNDED_TEST)
    if(ticks>=D56_STOP_TICKS) done=true;
#endif

    /* The only candidate authority is M10-A's own FIFO ready queue.
     * Rotation through supervisor gives service within three real ticks. */
    unsigned to=D56_STACKS;
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    if(!done && !failed && tasks.ready_count!=0) {
        struct kernel_task_handle selected={0};
        if(d57_select(&d57_policy,&d57_sync,&owners,&tasks,&selected))
            to=index_of_slot(selected.slot);
        else {failed=true;reason="PRIORITY_GRAPH";to=D56_SUP_INDEX;}
    } else to=D56_SUP_INDEX;
#else
    if(!done && !failed && tasks.ready_count!=0)
        to=index_of_slot(tasks.ready_queue[0]);
    else to=D56_SUP_INDEX;
#endif

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
    struct kernel_task_handle destination=handles[to];
#ifdef SPIFFOS_M10_D56_BAD_GENERATION_TEST
    /* A verified physical frame does not confer scheduler authority.
     * Inject a stale target handle; M10-A must refuse without mutation,
     * then move the valid source to the safe supervisor for diagnostics. */
    if(from==0 && to==1 && !negative_rejected) {
        struct kernel_task_handle stale=destination;
        ++stale.generation;
        size_t prior_slot=tasks.current_slot;
        size_t prior_ready=tasks.ready_count;
        enum kernel_task_result rejected=kernel_task_irq_handoff(
            &tasks,handles[from],stale,&chosen);
        if(rejected!=KERNEL_TASK_BAD_HANDLE ||
           tasks.current_slot!=prior_slot || tasks.ready_count!=prior_ready) {
            failed=true;reason="STALE_MUTATED";active=false;
            return incoming;
        }
        negative_rejected=true;
        failed=true;reason="STALE_HANDLE_DENIED";
        to=D56_SUP_INDEX;
        if(!frame_safe(to,saved_frames[to],KERNEL_TASK_RUNNABLE)) {
            reason="NO_SAFE_SUPERVISOR";active=false;
            return incoming;
        }
        destination=handles[to];
    }
#endif
    if(kernel_task_irq_handoff(&tasks,handles[from],destination,
                              &chosen)!=KERNEL_TASK_OK ||
       chosen.slot!=destination.slot ||
       chosen.generation!=destination.generation) {
        failed=true;reason="M10_A_HANDOFF";
        active=false;
        return incoming;
    }
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    if(!d57_note_handoff(&d57_policy,&tasks,chosen)) {
        /* CPU frame already changed authoritatively. Return the verified
         * chosen frame and let the supervisor terminate diagnostically. */
        failed=true;reason="PRIORITY_BOOKKEEPING";
    }
#endif
    if(irq_switches!=UINT32_MAX) ++irq_switches;
    return saved_frames[to];
}

/* A worker genuinely consumes CPU without voluntarily yielding. It performs
 * nested IF=0 critical sections, proving no IRQ selector can enter while
 * the shared critical-depth counter is nonzero. */
__attribute__((noreturn)) static void worker_run(unsigned index)
{
    volatile uint64_t work=(uint64_t)index+1u;
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
    bool has_lock=false;
    bool lock_completed=false;
    if(index==0) {
        const uint64_t flags=enter_critical();
        if(d53_acquire(&d57_sync,&owners,&tasks,0,handles[index])!=D53_OK) {
            failed=true;reason="C1_OWNER_ACQUIRE";
        } else {
            has_lock=true;
            ++d58c_lock_acquires;
        }
        exit_critical(flags);
    }
#endif
    while(!done && !failed) {
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
        /* Release only after several genuine IRQ0 interruptions while
         * this exact worker was holding the real D5.3 rank-0 mutex.
         * A live-running owner changes the graph only at non-IRQ IF=0
         * safe point; all IRQ selections are read-only. */
        if(index==0 && has_lock && !lock_completed &&
           d58c_locked_source_irqs>=3) {
            const uint64_t flags=enter_critical();
            if(d53_release(&d57_sync,&owners,&tasks,0,handles[index])!=D53_OK) {
                failed=true;reason="C1_OWNER_RELEASE";
            } else {
                ++d58c_lock_releases;
                has_lock=false;
                lock_completed=true;
            }
            exit_critical(flags);
        }
#endif
        work=work*UINT64_C(6364136223846793005)+UINT64_C(1);
        ++iterations[index];
#ifdef SPIFFOS_M10_D59_PREVIEW_FAULT_TEST
        /* Test an actual worker-stack #UD. The existing fatal trap retains
         * all task backing. It must never continue or report retirement. */
        if(index==0 && slices[0]>=12) __asm__ volatile("ud2":::"memory");
#endif
        if((iterations[index]&UINT64_C(0x3fff))==0) {
#ifdef SPIFFOS_M10_D59B_CONTEXT_TEST
            /* First outer transaction on EACH real worker starts IF=0;
             * later transactions start IF=1. Verify inherited IF exactly. */
            const bool inherited_off=d59b_cycles[index]==0;
            uint64_t inherited_flags=0;
            if(inherited_off)
                __asm__ volatile("pushfq; popq %0; cli":"=r"(inherited_flags)::"memory");
            uint32_t cookie=0;
            if(!d59b_enter(&d59b_context,&cookie)) {
                failed=true;reason="D59B_ENTER";
                for(;;) __asm__ volatile("cli; hlt":::"memory");
            }
            const uint64_t before=d59b_tsc();
            if(!memory_boot_d59b_frame_cycle(&d59b_context)) {
                failed=true;reason="D59B_FRAME";
                for(;;) __asm__ volatile("cli; hlt":::"memory");
            }

#ifdef SPIFFOS_M10_D59B_SERVICE_TEST
            if(!memory_boot_d59b_service_cycle(&d59b_context,index)) {
                failed=true;reason="D59B_SERVICE";
                for(;;) __asm__ volatile("cli; hlt":::"memory");
            }
            ++d59b_service_cycles[index];
#endif
#ifdef SPIFFOS_M10_D59B_PAGE_CLAIM_TEST
            if(!memory_boot_d59b_page_cycle(&d59b_context)) {
                failed=true;reason="D59B_PAGE";
                for(;;) __asm__ volatile("cli; hlt":::"memory");
            }
            ++d59b_page_cycles[index];
#endif
            const uint64_t after=d59b_tsc();
            if(after>=before && after-before>d59b_max_body_cycles)
                d59b_max_body_cycles=after-before;
            ++d59b_cycles[index];
            if(!d59b_exit(&d59b_context,cookie)) {
                failed=true;reason="D59B_EXIT";
                for(;;) __asm__ volatile("cli; hlt":::"memory");
            }
            uint64_t restored_flags=0;
            __asm__ volatile("pushfq; popq %0":"=r"(restored_flags)::"memory");
            if(((restored_flags&UINT64_C(0x200))==0)!=inherited_off) {
                failed=true;reason="D59B_IF_RESTORE";
                for(;;) __asm__ volatile("cli; hlt":::"memory");
            }
            if(inherited_off && (inherited_flags&UINT64_C(0x200))!=0)
                __asm__ volatile("sti":::"memory");
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
            if(!memory_boot_d59b_direct_refusals(&d59b_context,index)) {
                failed=true;reason="D59B_DIRECT_CALL";
                for(;;) __asm__ volatile("cli; hlt":::"memory");
            }
            ++d59b_direct_cycles[index];
#endif
#endif
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

#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
/* Called exclusively on supervisor stack inside an IF=0 critical region;
 * consuming diagnostic observations never transfers any mutex ownership. */
static void d58_supervisor_drain(void)
{
    while(d58_observations.count!=0 && !d58_observations.faulted) {
        struct d58_notice witness={0};
        if(!d58_take_supervisor(&d58_observations,&tasks,&witness,
                                false,true,true)) {
            failed=true;reason="D58_SUPERVISOR_DRAIN";
            return;
        }
        if(d58_drained==UINT32_MAX) {
            failed=true;reason="D58_DRAIN_OVERFLOW";
            return;
        }
        ++d58_drained;
    }
    if(d58_observations.faulted) {
        failed=true;reason="D58_QUEUE_FAULT";
    }
}
#endif

/* Supervisor executes on a third, registered stack and records only
 * completed work, never mere opportunities chosen by the IRQ scheduler. */
__attribute__((noreturn)) static void supervisor_run(void)
{
    /* Enable the IRQ selector ONLY after cooperative first-entry has
     * established the actual supervisor-owned stack and exact handle. */
    const uint64_t entry=enter_critical();
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    /* Guard startup must complete while selector is still inactive,
     * so no interval exists where a bootstrap critical owner is forged. */
    exit_critical(entry);
    active=true;
#else
    active=true;
    exit_critical(entry);
#endif
    while(!done && !failed) {
        const uint64_t prior=enter_critical();
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
        d58_supervisor_drain();
#endif
#ifdef SPIFFOS_M10_D59_PREVIEW
        /* Count completed service once per observed hardware tick. A busy
         * supervisor loop may revisit the same tick many times. */
        if(ticks != service.last_completed_service_tick &&
           !d55c_tick_service_completed(&service,true,true,ticks)) {
#else
        if(!d55c_tick_service_completed(&service,true,true,ticks)) {
#endif
            failed=true;reason="SERVICE_COMPLETION";
        }
#if defined(SPIFFOS_M10_D57_IRQ_TEST) && defined(SPIFFOS_M10_D57_BAD_GRAPH_TEST)
        /* Negative injection happens in a supervisor-owned IF=0 safe
         * point, never in the IRQ handler. The next IRQ must refuse
         * read-only selection of this corrupt D5.3 generation graph. */
        if(!negative_rejected && slices[0]>=2) {
            ++d57_sync.profiles[handles[1].slot].handle.generation;
            negative_rejected=true;
        }
#endif
        exit_critical(prior);
        __asm__ volatile("pause":::"memory");
    }
    const uint64_t prior=enter_critical();
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
    d58_supervisor_drain();
#endif
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
    if(index>=D56_SUP_INDEX || base==0 ||
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
bool m10_d56_guest_smoke(bool (*clock_ms)(uint64_t *)
#ifdef SPIFFOS_M10_D59_PREVIEW
    ,void (*foreground)(void)
#endif
)
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
       !d55c_tick_init(&service,0,D56_MAX_SERVICE_GAP)
#ifdef SPIFFOS_M10_D57_IRQ_TEST
       || !d53_init(&d57_sync) || !d57_policy_init(&d57_policy) ||
       !d57_guard_init(&d57_guard)
#endif
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
       || !d58_queue_init(&d58_observations)
#endif
       ) {
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
#ifdef SPIFFOS_M10_D59_PREVIEW
       !prepare_worker(D56_FG_INDEX,foreground) ||
#endif
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
    for(unsigned i=0;i<D56_SUP_INDEX && ready;++i)
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
#ifdef SPIFFOS_M10_D57_IRQ_TEST
    /* Enroll true M10-A generations outside IRQ after successful owner
     * binding; the IRQ may only observe D5.3 effective classes. */
    if(ready) ready=d53_enroll(&d57_sync,&owners,&tasks,handles[0],
            KERNEL_PRIORITY_INTERACTIVE)==D53_OK &&
        d53_enroll(&d57_sync,&owners,&tasks,handles[1],
            KERNEL_PRIORITY_TIME_SENSITIVE)==D53_OK &&
#ifdef SPIFFOS_M10_D59_PREVIEW
        d53_enroll(&d57_sync,&owners,&tasks,handles[D56_FG_INDEX],
            KERNEL_PRIORITY_TIME_SENSITIVE)==D53_OK &&
#endif
        d53_check(&d57_sync,&owners,&tasks);
#endif
    struct kernel_task_handle dispatched={0};
    if(ready) ready=kernel_task_dispatch(&tasks,&dispatched)==KERNEL_TASK_OK &&
        dispatched.slot==handles[D56_SUP_INDEX].slot;
#ifdef SPIFFOS_M10_D59B_CONTEXT_TEST
    if(ready) ready=d59b_context_init(&d59b_context,&d57_guard,&tasks,&owners);
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
    /* Actual RSP is still the surviving boot stack; selector inactive. */
    if(ready) ready=d59b_memory_bind(&d59b_context);
#endif
#endif
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
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
    /* No stack release is attempted if exact off-stack unbind is uncertain.
     * Preserve deliberate IRQ-negative reason; never repair a faulted guard. */
    if(!d59b_memory_unbind(&d59b_context)) {
        if(!failed) reason="D59B_MEMORY_UNBIND";
        exit_critical(clean);
        return false;
    }
#endif
    bool good=returned_to_boot && done && !failed &&
        irq_switches>=D56_MIN_WORKER_TICKS &&
        slices[0]>=D56_MIN_WORKER_TICKS &&
        slices[1]>=D56_MIN_WORKER_TICKS &&
#ifdef SPIFFOS_M10_D59_PREVIEW
        slices[D56_FG_INDEX]>=D56_MIN_WORKER_TICKS &&
#endif
        service.service_completions>=D56_MIN_WORKER_TICKS &&
        service.max_observed_gap_ticks<=D56_MAX_SERVICE_GAP &&
        critical_cycles!=0 &&
#ifdef SPIFFOS_M10_D59B_CONTEXT_TEST
        d59b_cycles[0]!=0 && d59b_cycles[1]!=0 &&
        d59b_real_irq_entries>=D56_MIN_WORKER_TICKS && d59b_irq_depth==0 &&
        d59b_max_body_cycles!=0 &&
#endif
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
        d58c_lock_acquires==1 && d58c_lock_releases==1 &&
        d58c_locked_source_irqs>=3 &&
        owners.task[handles[0].slot].owned_locks==0 &&
        !d57_sync.locks[0].owned &&
#endif
#ifdef SPIFFOS_M10_D57_IRQ_TEST
        !d57_guard.faulted && d57_guard.depth==0 &&
        d57_policy.decisions>0 &&
        d53_check(&d57_sync,&owners,&tasks) &&
#endif
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
        !d58_observations.faulted && d58_observations.count==0 &&
        d58_drained==ticks && d58_drained>=D56_MIN_WORKER_TICKS &&
#endif
        kernel_task_check(&tasks) &&
        kernel_task_resources_check(&owners,&tasks);
    for(unsigned i=0;i<D56_SUP_INDEX;++i)
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
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
    if(!d59b_memory_retirement_complete(&d59b_context)) good=false;
#endif
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
/* Report only after successful return to surviving boot context. These
 * measures are from real IRQ0 ticks and completed supervisor work. */
static void report_number(bool (*write)(const char *),uint64_t value)
{
    char digits[21];
    unsigned position=20;
    digits[position]='\0';
    do {
        digits[--position]=(char)('0'+value%10u);
        value/=10u;
    } while(value!=0);
    (void)write(&digits[position]);
}

/* Serial is strictly outside IRQ0; never inside a worker or lock mutation. */
void m10_d56_report(bool (*write)(const char *))
{
    if(write==NULL || !returned_to_boot || failed) return;
#ifdef SPIFFOS_M10_D59B_CONTEXT_TEST

#ifdef SPIFFOS_M10_D59B_SERVICE_TEST
    (void)write("D59B_WORKER0_SERVICE_CYCLES:");
    report_number(write,d59b_service_cycles[0]);
    (void)write("\r\nD59B_WORKER1_SERVICE_CYCLES:");
    report_number(write,d59b_service_cycles[1]);
    (void)write("\r\nSPIFFOS_M10_D59B_SERVICE_IRQ_OK\r\n");
#endif
#ifdef SPIFFOS_M10_D59B_PAGE_CLAIM_TEST
    (void)write("D59B_WORKER0_PAGE_CYCLES:");
    report_number(write,d59b_page_cycles[0]);
    (void)write("\r\nD59B_WORKER1_PAGE_CYCLES:");
    report_number(write,d59b_page_cycles[1]);
    (void)write("\r\nSPIFFOS_M10_D59B_PAGE_IRQ_OK\r\n");
#endif
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
    (void)write("D59B_WORKER0_DIRECT_CYCLES:");
    report_number(write,d59b_direct_cycles[0]);
    (void)write("\r\nD59B_WORKER1_DIRECT_CYCLES:");
    report_number(write,d59b_direct_cycles[1]);
    (void)write("\r\nSPIFFOS_M10_D59B_MEMORY_BINDING_OK\r\n");
#endif
    (void)write("D59B_WORKER0_FRAME_CYCLES:");
    report_number(write,d59b_cycles[0]);
    (void)write("\r\nD59B_WORKER1_FRAME_CYCLES:");
    report_number(write,d59b_cycles[1]);
    (void)write("\r\nD59B_AUTHENTIC_IRQ_ENTRIES:");
    report_number(write,d59b_real_irq_entries);
    (void)write("\r\nD59B_MAX_TRANSACTION_BODY_TSC:");
    report_number(write,d59b_max_body_cycles);
    (void)write("\r\nD59B_MAX_EXCLUSION_TSC:");
    report_number(write,d59b_context.max_exclusion_tsc);
    (void)write("\r\nSPIFFOS_M10_D59B_CONTEXT_IRQ_OK\r\n");
#endif
    (void)write("D56_WORKER0_TICKS:");
    report_number(write,slices[0]);
    (void)write("\r\nD56_WORKER1_TICKS:");
    report_number(write,slices[1]);
    (void)write("\r\nD56_SUPERVISOR_SERVICE_COMPLETIONS:");
    report_number(write,service.service_completions);
    (void)write("\r\nD56_MAX_UNSERVICED_IRQ_TICKS:");
    report_number(write,service.max_observed_gap_ticks);
    (void)write("\r\nD56_CRITICAL_SECTIONS:");
    report_number(write,critical_cycles);
    (void)write("\r\nD56_IRQ_SWITCHES:");
    report_number(write,irq_switches);
#ifdef SPIFFOS_M10_D58C_LOCK_IRQ_TEST
    (void)write("\r\nD58C_LOCKED_SOURCE_IRQS:");
    report_number(write,d58c_locked_source_irqs);
    (void)write("\r\nD58C_LOCK_ACQUIRES:");
    report_number(write,d58c_lock_acquires);
    (void)write("\r\nD58C_LOCK_RELEASES:");
    report_number(write,d58c_lock_releases);
#endif
#ifdef SPIFFOS_M10_D58_NOTIFY_TEST
    (void)write("\r\nD58_IRQ_NOTICES_DRAINED:");
    report_number(write,d58_drained);
#endif
    (void)write("\r\n");
}
#endif
