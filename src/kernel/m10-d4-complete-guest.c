/* Purpose: M10-D4 completion proof of four-class real IRQ scheduling.
 * Responsibilities: four independent frame-backed worker stacks; real worker
 * self-block, supervisor wake/quarantine/recover, bounded service accounting.
 * Dependencies: D1 pure selection, D4 state API, M10-B stack provider,
 * D2 full 20-qword IRQ frame and M9 100Hz PIT0/APIC path.
 * Invariants: one ring0 boot CPU; all frame ownership survives every IRQ;
 * only boot dispatcher releases stacks after complete IRETQ; no FPU/SIMD,
 * locks/allocations/serial in ISR. Test-only build flag, not default kernel.
 * Reference: docs/decisions/ADR-m10-d4-priority-supervisor-proof.md.
 */
#include "m10-d4-task-control.h"
#include "m10-d2-irq-frame.h"
#include "memory-boot.h"
#include "x86-task-context.h"
#ifdef SPIFFOS_M10_D55C_TICK_TEST
#include "m10-d55c-frame-owner.h"
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SPIFFOS_M10_D4_COMPLETE_TEST
enum { D4_SUP_INDEX=D4_TASK_WORKERS, D4_TOTAL=D4_TASK_WORKERS+1,
       D4_STACK_CANARY=64, D4_MIN_SLICES=12, D4_MAX_TICKS=700 };
static struct d4_task_control state;
#ifdef SPIFFOS_M10_D55C_TICK_TEST
/* Observed IRQ ticks belong to this D4 diagnostic only; no new scheduler.
 * Only a genuinely executed supervisor loop iteration records service. */
static struct d55c_tick_service d55c_watch;
#endif
static struct d4_task_lock inversion_lock;
static volatile bool low_owns_lock,high_waited,low_released_lock,
                     high_released_lock;
static volatile bool active,done,failed;
static volatile uint32_t running=D4_TASK_SUPERVISOR, ticks, switches;
static volatile uint32_t slices[D4_TOTAL];
static volatile uint64_t iterations[D4_TASK_WORKERS];
static volatile bool worker_blocked_once;
static uint64_t *frame[D4_TOTAL];
static uint8_t *stack[D4_TASK_WORKERS];
static uint64_t cs_value,ss_value;
static uint32_t supervisor_max_gap, supervisor_since;
static uint64_t maximum_ready_wait[D4_TASK_WORKERS],elapsed_ms;
static uint32_t blocked_ticks, wake_events, quarantine_events, recovery_events;
static bool quarantine_rejected_bad_frame;
static const char *failure_reason="UNSET";

/* Report static storage only after the diagnostic has returned to boot.
 * Do not serialize a kernel error while executing on a quarantined stack. */
const char *m10_d4_complete_failure_reason(void)
{
    return failure_reason;
}

/* Interrupt exclusion is required for every state/owner transition.
 * Save caller IF and restore precisely its previous enabled/disabled state;
 * no locks or external I/O are performed. */
static uint64_t irq_disable(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void irq_restore(uint64_t flags)
{
    if(flags&UINT64_C(0x200)) __asm__ volatile("sti" : : : "memory");
}

/* A CPU-bound worker cannot voluntarily select another task. Background
 * exercises genuine self-block: after it blocks, only supervisor wakeup
 * makes this SAME independently saved task context runnable again. */
__attribute__((noreturn))
static void worker_run(unsigned id)
{
    volatile uint64_t local=(uint64_t)(id+1u);
    for(;;) {
        local=local*UINT64_C(6364136223846793005)+UINT64_C(1);
        ++iterations[id];
        /* Real lock inversion: Background worker owns a kernel lock;
         * Time-sensitive worker then blocks behind it and donates its
         * effective priority until owner releases. Every mutation uses
         * IF=0 exclusion; both workers remain on their own saved stacks. */
        if(id==0 && !low_owns_lock) {
            uint64_t previous=irq_disable();
            if(d4_task_lock_acquire(&state,&inversion_lock,0)!=D4_TASK_OK)
                failed=true;
            else low_owns_lock=true;
            irq_restore(previous);
        }
        if(id==3 && low_owns_lock && !high_waited) {
            uint64_t previous=irq_disable();
            if(d4_task_lock_acquire(&state,&inversion_lock,3)!=D4_TASK_WAITING)
                failed=true;
            else high_waited=true;
            irq_restore(previous);
            /* The task is actually BLOCKED and absent from all snapshots.
             * It resumes here only after Background transfers the lock. */
            while(state.worker[3].status==D4_TASK_BLOCKED)
                __asm__ volatile("pause" : : : "memory");
        }
        if(id==0 && high_waited && !low_released_lock) {
            uint64_t previous=irq_disable();
            if(state.worker[0].priority!=KERNEL_PRIORITY_TIME_SENSITIVE ||
               d4_task_lock_release(&state,&inversion_lock,0)!=D4_TASK_OK)
                failed=true;
            else low_released_lock=true;
            irq_restore(previous);
        }
        if(id==3 && high_waited && low_released_lock &&
           !high_released_lock && inversion_lock.owner==3) {
            uint64_t previous=irq_disable();
            if(d4_task_lock_release(&state,&inversion_lock,3)!=D4_TASK_OK)
                failed=true;
            else high_released_lock=true;
            irq_restore(previous);
        }
        if(id==0 && low_released_lock && high_released_lock &&
           !worker_blocked_once && iterations[0]>=1000) {
            uint64_t before=irq_disable();
            if(running!=0 ||
               d4_task_block(&state,0,running)!=D4_TASK_OK)
                failed=true;
            else worker_blocked_once=true;
            irq_restore(before);
            /* Distinct independent self-block/wakeup exercise follows
             * ownership transfer. No cooperative schedule/yield call. */
            while(state.worker[0].status==D4_TASK_BLOCKED)
                __asm__ volatile("pause" : : : "memory");
        }
        __asm__ volatile("pause" : : : "memory");
    }
}

/* First-run worker entry is a trampoline with immutable identity; no
 * arguments need to be passed through an interrupted hardware GPR frame. */
__attribute__((noreturn)) static void worker0(void){worker_run(0);}
__attribute__((noreturn)) static void worker1(void){worker_run(1);}
__attribute__((noreturn)) static void worker2(void){worker_run(2);}
__attribute__((noreturn)) static void worker3(void){worker_run(3);}

/* Each first-run frame occupies verified mapped memory, with an 8 mod16
 * SysV entry stack and full long-mode CS/SS/RFLAGS/RSP/RIP values.
 * This is never a return-to-caller address. */
static bool prepare_frame(unsigned id,void (*entry)(void))
{
    if(id>=D4_TASK_WORKERS || stack[id]==NULL || entry==NULL)
        return false;
    uintptr_t base=(uintptr_t)stack[id];
    if(base>UINTPTR_MAX-X86_TASK_STACK_BYTES ||
       (base+X86_TASK_STACK_BYTES)%16u!=0) return false;
    uintptr_t top=base+X86_TASK_STACK_BYTES;
    uintptr_t first_rsp=top-sizeof(uint64_t);
    uint64_t *words=(uint64_t *)(first_rsp-
                                  M10_D2_FRAME_QWORDS*sizeof(uint64_t));
    for(size_t i=0;i<M10_D2_FRAME_QWORDS;++i) words[i]=0;
    *(uint64_t *)first_rsp=0; /* A worker returning would be fatal. */
    words[M10_D2_RIP]=(uintptr_t)entry;
    words[M10_D2_CS]=cs_value;
    words[M10_D2_RFLAGS]=UINT64_C(0x202);
    words[M10_D2_RSP]=first_rsp;
    words[M10_D2_SS]=ss_value;
    frame[id]=words;
    return true;
}

/* Validate the COMPLETE saved frame before even reading its segments.
 * Bootstrap frame is trusted M9 IRQ-entry state on permanent boot stack;
 * worker pointers must stay within their own exact mapped 16KiB owner. */
static bool valid_frame(unsigned id,const uint64_t *words)
{
    if(words==NULL || ((uintptr_t)words&7u)!=0 ||
       id>D4_SUP_INDEX) return false;
    if(id<D4_TASK_WORKERS) {
        uintptr_t base=(uintptr_t)stack[id];
        if(base==0 || base>UINTPTR_MAX-X86_TASK_STACK_BYTES)
            return false;
        uintptr_t ptr=(uintptr_t)words;
        uintptr_t top=base+X86_TASK_STACK_BYTES;
        if(ptr<base+D4_STACK_CANARY ||
           ptr>top-M10_D2_FRAME_QWORDS*sizeof(uint64_t))
            return false;
        if(words[M10_D2_RSP]<base+D4_STACK_CANARY ||
           words[M10_D2_RSP]>top) return false;
    }
    return words[M10_D2_CS]==cs_value &&
           words[M10_D2_SS]==ss_value &&
           (words[M10_D2_RFLAGS]&UINT64_C(0x202))==UINT64_C(0x202);
}

/* This runs only at IRQ EXIT after M9 tick accounting and one LAPIC EOI.
 * Interrupt gates keep IF=0, so the state owner and stack map cannot race
 * with boot dispatcher mutations. Never allocate/free/log in this path. */
uint64_t *m10_d4_complete_irq_select(uint64_t *incoming)
{
    if(!active) return incoming;
    uint32_t current=running;
    unsigned index=current==D4_TASK_SUPERVISOR ?
                   D4_SUP_INDEX:current;
    if(index>D4_SUP_INDEX || !valid_frame(index,incoming)) {
        failed=true;active=false;running=D4_TASK_SUPERVISOR;
        return frame[D4_SUP_INDEX]!=NULL ? frame[D4_SUP_INDEX]:incoming;
    }
    frame[index]=incoming;
    ++ticks;
#ifdef SPIFFOS_M10_D55C_TICK_TEST
    /* A true hardware IRQ0 tick advances even when workers never yield.
     * The monitor never equates an IRQ dispatch with supervisor service. */
    if(!d55c_tick_observe(&d55c_watch,true,ticks))
        failed=true;
#endif
    ++slices[index];
    if(ticks>=D4_MAX_TICKS || !d4_task_check(&state))
        failed=true;

    /* Compute bounded supervisor response gap over actual IRQ dispatches.
     * At 100Hz, a gap <=4 dispatches means the reserved supervisor
     * receives a chance at least every ~40ms in the verified QEMU model.
     * This is NOT a hard realtime guarantee on physical chipsets. */
    if(current==D4_TASK_SUPERVISOR) supervisor_since=0;
    else if(supervisor_since<UINT32_MAX) ++supervisor_since;
    if(supervisor_since>supervisor_max_gap)
        supervisor_max_gap=supervisor_since;
    if(state.worker[0].status==D4_TASK_BLOCKED) ++blocked_ticks;

    bool all_served=true;
    for(unsigned i=0;i<D4_TASK_WORKERS;++i)
        if(slices[i]<D4_MIN_SLICES || iterations[i]==0)
            all_served=false;
    if(current==D4_TASK_SUPERVISOR && all_served &&
       worker_blocked_once && wake_events>0 &&
       low_owns_lock && high_waited &&
       low_released_lock && high_released_lock &&
       inversion_lock.owner==UINT32_MAX &&
       quarantine_events>0 && recovery_events>0 &&
       quarantine_rejected_bad_frame &&
       state.supervisor_dispatches>=D4_MIN_SLICES)
        done=true;
    if(done || failed) {
        active=false;
        running=D4_TASK_SUPERVISOR;
        return frame[D4_SUP_INDEX];
    }

    uint32_t selected=UINT32_MAX;
    uint64_t decision=state.decision_sequence;
    if(d4_task_choose(&state,&selected)!=D4_TASK_OK ||
       (decision%KERNEL_PRIORITY_SUPERVISOR_INTERVAL==
            KERNEL_PRIORITY_SUPERVISOR_INTERVAL-1 &&
        selected!=D4_TASK_SUPERVISOR)) {
        failed=true;active=false;running=D4_TASK_SUPERVISOR;
        return frame[D4_SUP_INDEX];
    }
    unsigned target=selected==D4_TASK_SUPERVISOR ?
                    D4_SUP_INDEX:selected;
    if(!valid_frame(target,frame[target]) ||
       d4_task_note_dispatch(&state,selected)!=D4_TASK_OK) {
        failed=true;active=false;running=D4_TASK_SUPERVISOR;
        return frame[D4_SUP_INDEX];
    }
    for(unsigned i=0;i<D4_TASK_WORKERS;++i)
        if(state.worker[i].status==D4_TASK_READY &&
           state.worker[i].waiting_dispatches>maximum_ready_wait[i])
            maximum_ready_wait[i]=state.worker[i].waiting_dispatches;
    if(selected!=current) ++switches;
    running=selected;
    return frame[target];
}

/* The supervisor is a *real context* on the surviving boot stack.
 * All blocked/wake and quarantine mutations are performed with IF=0
 * from this context. The CPU must not be using the target worker stack. */
static void supervisor_service(void)
{
    uint64_t before=irq_disable();
    if(running!=D4_TASK_SUPERVISOR) {
        failed=true;
        irq_restore(before);
        return;
    }
#ifdef SPIFFOS_M10_D55C_TICK_TEST
    /* This is an actually executed supervisor loop on the surviving boot
     * stack, after the IRQ selection has returned via IRETQ. */
    if(!d55c_tick_service_completed(&d55c_watch,
            true,true,ticks)) failed=true;
#endif
    if(worker_blocked_once && state.worker[0].status==D4_TASK_BLOCKED &&
       blocked_ticks>=5 && wake_events==0) {
        if(d4_task_wake(&state,0,D4_TASK_SUPERVISOR)!=D4_TASK_OK)
            failed=true;
        else ++wake_events;
    }
    /* A captured worker2 stack frame can be quarantined without affecting
     * its storage. Inject a wrong CS to PROVE recovery rejects corrupt
     * state, repair the existing trusted frame, then validate and wake. */
    if(state.worker[2].dispatch_count>=2 &&
       quarantine_events==0 && frame[2]!=NULL) {
        if(d4_task_quarantine(&state,2,D4_TASK_SUPERVISOR,running)!=D4_TASK_OK)
            failed=true;
        else ++quarantine_events;
    }
#ifdef SPIFFOS_M10_D4_BAD_FRAME_TEST
    /* Diagnostic negative: damage an inactive, retained worker frame.
     * IRQ-exit selector MUST refuse it and return to the safe supervisor. */
    if(state.worker[3].dispatch_count>=2 && frame[3]!=NULL)
        frame[3][M10_D2_CS]=0;
#endif
    if(quarantine_events>0 && recovery_events==0 &&
       state.supervisor_dispatches>=14 &&
       state.worker[2].status==D4_TASK_QUARANTINED) {
        uint64_t original=frame[2][M10_D2_CS];
        frame[2][M10_D2_CS]=0;
        bool bad=valid_frame(2,frame[2]);
        enum d4_task_result denied=d4_task_recover(&state,2,
                                           D4_TASK_SUPERVISOR,bad);
        quarantine_rejected_bad_frame=!bad && denied==D4_TASK_CORRUPT &&
            state.worker[2].status==D4_TASK_QUARANTINED;
        frame[2][M10_D2_CS]=original;
        if(!quarantine_rejected_bad_frame ||
           !valid_frame(2,frame[2]) ||
           d4_task_recover(&state,2,D4_TASK_SUPERVISOR,true)!=D4_TASK_OK)
            failed=true;
        else ++recovery_events;
    }
    irq_restore(before);
}

/* Boot-CPU proof of full four-band execution. Real interrupts drive every
 * worker; boot supervisor drives readiness/recovery safely outside IRQ.
 * Always release worker stack frames only AFTER returning via IRETQ to
 * this permanent boot stack and disabling interrupts again. */
bool m10_d4_complete_guest_smoke(bool (*clock_ms)(uint64_t *))
{
    if(clock_ms==NULL) {failure_reason="NO_TIMER";return false;}
    uint64_t entry=irq_disable();
    if(!(entry&UINT64_C(0x200))) {
        failure_reason="IRQ0_NOT_ENABLED";return false;
    }
#ifdef SPIFFOS_M10_D55C_TICK_TEST
    /* A physical-time deadline is not claimed. D4 ticks are 100Hz
     * QEMU observations; a strict >8-tick gap is diagnostic failure.
     * The negative fixture deliberately tightens it to 1 tick. */
#ifdef SPIFFOS_M10_D55C_BAD_BUDGET_TEST
    if(!d55c_tick_init(&d55c_watch,0,1)) {
#else
    if(!d55c_tick_init(&d55c_watch,0,8)) {
#endif
        failure_reason="SERVICE_MONITOR_INIT";irq_restore(entry);return false;
    }
#endif
    if(!d4_task_init(&state) ||
       !d4_task_lock_init(&inversion_lock) ||
       !memory_boot_m10_task_stack_failure_probe()) {
        failure_reason="TASK_OR_ALLOCATOR_INIT";irq_restore(entry);return false;
    }
    unsigned prepared=0;
    for(;prepared<D4_TASK_WORKERS;++prepared) {
        void *owned=NULL;
        if(!memory_boot_m10_task_stack_prepare(prepared,&owned)) break;
        stack[prepared]=(uint8_t *)owned;
    }
    if(prepared!=D4_TASK_WORKERS) {
        for(unsigned i=prepared;i>0;--i)
            (void)memory_boot_m10_task_stack_release(i-1);
        failure_reason="STACK_ALLOCATE";irq_restore(entry);return false;
    }
    uint16_t cs16=0,ss16=0;
    __asm__ volatile("movw %%cs,%0" : "=r"(cs16));
    __asm__ volatile("movw %%ss,%0" : "=r"(ss16));
    cs_value=cs16;ss_value=ss16;
    void (*const entries[D4_TASK_WORKERS])(void)={
        worker0,worker1,worker2,worker3
    };
    bool framed=true;
    for(unsigned i=0;i<D4_TASK_WORKERS;++i) {
        for(size_t j=0;j<D4_STACK_CANARY;++j)
            stack[i][j]=UINT8_C(0xa7);
        if(!prepare_frame(i,entries[i])) framed=false;
    }
    uint64_t begin=0,end=0;
    (void)clock_ms(&begin); /* M9 first IRQ may not yet have fired. */
    if(framed) {
        running=D4_TASK_SUPERVISOR;
        active=true;
        irq_restore(entry);
        while(!done && !failed) {
            supervisor_service();
            __asm__ volatile("pause" : : : "memory");
        }
    } else {
        failure_reason="FRAME_PREPARE";
        irq_restore(entry);
    }
    uint64_t exclusion=irq_disable();
    /* Read ownership and quotas after the selected IRETQ has returned. */
    bool valid=framed && done && !failed &&
        running==D4_TASK_SUPERVISOR &&
        state.supervisor_dispatches>=D4_MIN_SLICES &&
        supervisor_max_gap<=KERNEL_PRIORITY_SUPERVISOR_INTERVAL &&
        switches>=D4_MIN_SLICES*D4_TASK_WORKERS &&
        blocked_ticks>=5 && wake_events>0 &&
        low_owns_lock && high_waited &&
        low_released_lock && high_released_lock &&
        inversion_lock.owner==UINT32_MAX &&
        quarantine_events>0 && recovery_events>0 &&
        quarantine_rejected_bad_frame &&
        clock_ms(&end) && end>begin;
#ifdef SPIFFOS_M10_D55C_TICK_TEST
    valid=valid && !d55c_watch.faulted &&
        d55c_watch.service_completions>=D4_MIN_SLICES &&
        d55c_watch.max_observed_gap_ticks<=d55c_watch.deadline_ticks;
#endif
#ifdef SPIFFOS_M10_D4_BAD_CANARY_TEST
    /* Inject guard corruption AFTER leaving worker stacks and masking IRQs.
     * Fail closed on owner-guard mismatch, but still release backing. */
    stack[0][0]=0;
#endif
    for(unsigned i=0;i<D4_TASK_WORKERS;++i) {
        if(slices[i]<D4_MIN_SLICES || iterations[i]==0)
            valid=false;
        for(size_t j=0;j<D4_STACK_CANARY;++j)
            if(stack[i][j]!=UINT8_C(0xa7)) valid=false;
    }
    if(valid) elapsed_ms=end-begin;
    if(!valid && failure_reason[0]=='U')
        failure_reason="D4_QUOTA_OR_RECOVERY";
    bool clean=true;
    for(unsigned i=D4_TASK_WORKERS;i>0;--i)
        if(!memory_boot_m10_task_stack_release(i-1)) clean=false;
    irq_restore(exclusion);
    if(!clean) {
        failure_reason="STACK_RELEASE";return false;
    }
    return valid;
}
/* Minimal decimal encoding avoids libc/printf in this freestanding kernel.
 * This is called only AFTER D4 has ended and all worker stacks are freed;
 * never from IRQ context or a worker's saved stack. */
static bool metric_number(bool (*write)(const char *),uint64_t n)
{
    char digits[21];
    unsigned start=20;
    digits[start]='\0';
    do {
        digits[--start]=(char)('0'+n%10u);
        n/=10u;
    } while(n!=0);
    return write(&digits[start]);
}

/* Publish exact diagnostic metrics via the caller's existing boot serial
 * sink. Each value was counted from actual 100Hz IRQ dispatches or measured
 * by the monotonic timer; these are bounded test observations, not hard
 * physical-machine realtime guarantees. */
void m10_d4_complete_report(bool (*write)(const char *))
{
    if(write==NULL || !done || failed) return;
    static const char *const worker_labels[D4_TASK_WORKERS]={
        "D4_WORKER_BACKGROUND_SLICES:",
        "D4_WORKER_NORMAL_SLICES:",
        "D4_WORKER_INTERACTIVE_SLICES:",
        "D4_WORKER_TIME_SENSITIVE_SLICES:"
    };
    for(unsigned i=0;i<D4_TASK_WORKERS;++i) {
        (void)write(worker_labels[i]);
        (void)metric_number(write,slices[i]);
        (void)write("\r\n");
    }
#ifdef SPIFFOS_M10_D55C_TICK_TEST
    (void)write("D55C_VERIFIED_SUPERVISOR_SERVICE_COMPLETIONS:");
    (void)metric_number(write,d55c_watch.service_completions);
    (void)write("\r\nD55C_MAX_OBSERVED_UNSERVICED_TICKS:");
    (void)metric_number(write,d55c_watch.max_observed_gap_ticks);
    (void)write("\r\n");
#endif
    (void)write("D4_SUPERVISOR_DISPATCHES:");
    (void)metric_number(write,state.supervisor_dispatches);
    (void)write("\r\nD4_SUPERVISOR_MAX_GAP_TICKS:");
    (void)metric_number(write,supervisor_max_gap);
    (void)write("\r\nD4_TOTAL_IRQ_TICKS:");
    (void)metric_number(write,ticks);
    (void)write("\r\nD4_ELAPSED_MS:");
    (void)metric_number(write,elapsed_ms);
    (void)write("\r\nD4_BLOCKED_TICKS:");
    (void)metric_number(write,blocked_ticks);
    (void)write("\r\nD4_WAKE_EVENTS:");
    (void)metric_number(write,wake_events);
    (void)write("\r\nD4_QUARANTINE_EVENTS:");
    (void)metric_number(write,quarantine_events);
    (void)write("\r\nD4_VALIDATED_RECOVERY_EVENTS:");
    (void)metric_number(write,recovery_events);
    (void)write("\r\nD4_PRIORITY_INHERITANCE_OK:");
    (void)metric_number(write,low_released_lock&&high_released_lock ? 1u:0u);
    (void)write("\r\n");
}
#endif
