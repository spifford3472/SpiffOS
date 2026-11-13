/* Purpose: M10-D3 QEMU-only real PIT0 interrupt-driven ring-0 handoff.
 * Responsibilities: prepare two independently owned 16 KiB stacks, build
 * first-run long-mode IRETQ frames, and select completed IRQ exit frames.
 * Dependencies: M10-B mapped frame-backed stack service, M9 IRQ0, M10-D2
 * validated 20-qword saved frame, kernel timer service.
 * Invariants: single boot CPU, no floating point/SIMD, no page/heap calls
 * in IRQ; a stack is freed only from the original surviving boot stack.
 * No scheduler calls from a general-purpose IRQ handler. No production
 * preemption: linked only under SPIFFOS_M10_D3_PREEMPT_TEST.
 * Reference: docs/decisions/ADR-m10-d-preemption-proposal.md,
 * docs/operations/m10-d3-irq-handoff-20261113.md.
 */
#include "memory-boot.h"
#include "m10-d2-irq-frame.h"
#include "x86-task-context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SPIFFOS_M10_D3_PREEMPT_TEST
enum {
    D3_BOOT=0, D3_WORKER_A=1, D3_WORKER_B=2,
    D3_CONTEXTS=3, D3_WORKERS=2, D3_REQUIRED_SLICES=12,
    D3_MAX_IRQ_SLICES=600, D3_CANARY_BYTES=64
};
static volatile bool d3_active, d3_done, d3_failed;
static volatile uint32_t d3_running, d3_switches, d3_ticks;
static volatile uint32_t d3_slices[D3_CONTEXTS];
static volatile uint64_t d3_iterations[D3_WORKERS];
static uint64_t *d3_frame[D3_CONTEXTS];
static uint8_t *d3_stack[D3_WORKERS];
static uint64_t d3_cs, d3_ss;
static const char *d3_failure="UNSET";

/* Owned static string is valid indefinitely, including after task cleanup.
 * Called only on the boot stack, never from an interrupt. */
const char *m10_d3_failure_reason(void) { return d3_failure; }

/* Interrupt masking is required around frame allocation and publication.
 * The previous IF is restored rather than blindly enabling interrupts. */
static uint64_t d3_irq_disable(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

/* Restores prior interrupt enable state after boot-stack operations.
 * No worker or IRQ callback invokes this helper. */
static void d3_irq_restore(uint64_t flags)
{
    if((flags & UINT64_C(0x200))!=0)
        __asm__ volatile("sti" : : : "memory");
}

/* CPU-bound task A never yields, blocks or calls a scheduler.
 * Volatile stack-local and global counters expose actual resumed progress
 * under the real PIT0 handoff. This function must never return. */
__attribute__((noreturn))
static void d3_worker_a(void)
{
    volatile uint64_t local=UINT64_C(0x12345);
    for(;;) {
        local=local*UINT64_C(3)+UINT64_C(1);
        ++d3_iterations[0];
        __asm__ volatile("pause" : : : "memory");
    }
}

/* Independent entry point/stack, with different arithmetic and progress
 * counter. No task calls cooperative yield or changes interrupt state. */
__attribute__((noreturn))
static void d3_worker_b(void)
{
    volatile uint64_t local=UINT64_C(0x6789a);
    for(;;) {
        local=local*UINT64_C(5)+UINT64_C(3);
        ++d3_iterations[1];
        __asm__ volatile("pause" : : : "memory");
    }
}

/* Construct the exact M10-D2 20-word saved frame on a mapped stack.
 * For the System V ABI, first entry RSP is 8 mod 16, as after CALL.
 * The hardware IRQ return restores RSP from the saved long-mode frame;
 * no RET must be executed to enter a task, and no free is permitted here.
 * Called with IF=0, before publishing the frame to the IRQ selector. */
static bool d3_prepare_frame(size_t worker, void (*entry)(void))
{
    if(worker>=D3_WORKERS || d3_stack[worker]==NULL || entry==NULL)
        return false;
    uintptr_t base=(uintptr_t)d3_stack[worker];
    if(base>UINTPTR_MAX-X86_TASK_STACK_BYTES ||
       ((base+X86_TASK_STACK_BYTES)&15u)!=0) return false;
    uintptr_t top=base+X86_TASK_STACK_BYTES;
    uintptr_t first_rsp=top-sizeof(uint64_t);
    uint64_t *frame=(uint64_t *)(top-
        (uintptr_t)M10_D2_FRAME_QWORDS*sizeof(uint64_t)-
        sizeof(uint64_t));
    for(size_t i=0;i<M10_D2_FRAME_QWORDS;++i) frame[i]=0;
    /* The worker functions never return; a stray RET would be a bug.
     * The sentinel is only diagnostic and is not an executable address. */
    *(uint64_t *)first_rsp=0;
    frame[M10_D2_RIP]=(uint64_t)(uintptr_t)entry;
    frame[M10_D2_CS]=d3_cs;
    frame[M10_D2_RFLAGS]=UINT64_C(0x202);
    frame[M10_D2_RSP]=(uint64_t)first_rsp;
    frame[M10_D2_SS]=d3_ss;
    d3_frame[worker+1]=frame;
    return true;
}

/* Frame checker needs to exclude canary area and reject an unrelated
 * task's stack. The boot frame is borrowed from the original boot stack
 * and only becomes available on the first interrupted boot wait loop.
 * No task memory is modified when input validation fails. */
static bool d3_valid_frame(unsigned current, const uint64_t *frame)
{
    if(frame==NULL || ((uintptr_t)frame & 7u)!=0 ||
       current>=D3_CONTEXTS) return false;
    /* A frame from a worker must occupy its own mapped stack. Check
     * address arithmetic and the COMPLETE frame extent before dereferencing
     * any hardware words. No other CPU can free these frames while D3 is
     * active because all frame retirement is on the boot stack with IF=0. */
    if(current!=D3_BOOT) {
        uintptr_t base=(uintptr_t)d3_stack[current-1];
        uintptr_t ptr=(uintptr_t)frame;
        if(base==0 || base>UINTPTR_MAX-X86_TASK_STACK_BYTES)
            return false;
        uintptr_t top=base+X86_TASK_STACK_BYTES;
        if(ptr<base+D3_CANARY_BYTES ||
           ptr>top-M10_D2_FRAME_QWORDS*sizeof(uint64_t))
            return false;
        if(frame[M10_D2_RSP]<base+D3_CANARY_BYTES ||
           frame[M10_D2_RSP]>top) return false;
    }
    /* The original boot frame comes only from the M9 interrupt-entry
     * assembly, never from a user-supplied or untrusted pointer. */
    return frame[M10_D2_CS]==d3_cs &&
           frame[M10_D2_SS]==d3_ss &&
           (frame[M10_D2_RFLAGS]&UINT64_C(0x202))==UINT64_C(0x202);
}

/* Called by test-only assembly AFTER the existing C IRQ0 handler has
 * accounted its tick and written exactly one LAPIC EOI. The IRQ gate keeps
 * IF=0, no nested IRQ occurs and this selector performs no I/O, allocation,
 * serialization, locks, or cooperative switch. Its return value is a
 * retained full 20-word frame pointer selected for IRETQ. */
uint64_t *m10_d3_irq_select(uint64_t *interrupted_frame)
{
    if(!d3_active) return interrupted_frame;
    unsigned current=d3_running;
    if(!d3_valid_frame(current,interrupted_frame)) {
        d3_failed=true;
        d3_active=false;
        /* If a worker's frame was malformed, return only to a previously
         * validated bootstrap frame; otherwise preserve interrupted state
         * and let the bounded QEMU test fail instead of guessing a stack. */
        return current!=D3_BOOT && d3_frame[D3_BOOT]!=NULL ?
            d3_frame[D3_BOOT] : interrupted_frame;
    }
    d3_frame[current]=interrupted_frame;
    ++d3_ticks;
    ++d3_slices[current];
    if(d3_ticks>D3_MAX_IRQ_SLICES) d3_failed=true;
    if(current==D3_BOOT &&
       d3_slices[D3_WORKER_A]>=D3_REQUIRED_SLICES &&
       d3_slices[D3_WORKER_B]>=D3_REQUIRED_SLICES)
        d3_done=true;
    if(d3_done || d3_failed) {
        /* All stacks remain allocated; boot code will release them only
         * after this IRQ has IRETQ'd to its surviving dispatcher frame. */
        d3_active=false;
        d3_running=D3_BOOT;
        return d3_frame[D3_BOOT];
    }
    unsigned next=(current+1u)%D3_CONTEXTS;
    if(d3_frame[next]==NULL ||
       !d3_valid_frame(next,d3_frame[next])) {
        d3_failed=true;
        d3_active=false;
        d3_running=D3_BOOT;
        return d3_frame[D3_BOOT];
    }
    ++d3_switches;
    d3_running=next;
    return d3_frame[next];
}

/* Runs only from the original SpiffOS boot stack after live IRQ0 startup.
 * Frame ownership is published under IF=0, workers get time solely from
 * timer IRQ exits, and both stacks are released after returning to boot.
 * Fail-closed on allocation failure, invalid frame or missing progress.
 * The test runner imposes an external QEMU deadline for a lost IRQ. */
bool m10_d3_guest_smoke(bool (*clock_ms)(uint64_t *))
{
    if(clock_ms==NULL) { d3_failure="CLOCK_REQUIRED"; return false; }
    uint64_t entry_flags=d3_irq_disable();
    if((entry_flags&UINT64_C(0x200))==0) {
        d3_failure="IRQ0_NOT_ENABLED";
        return false; /* Preserve caller's disabled-IF state. */
    }
    if(!memory_boot_m10_task_stack_failure_probe()) {
        d3_failure="ALLOCATION_ROLLBACK";
        d3_irq_restore(entry_flags);
        return false;
    }
    void *a=NULL,*b=NULL;
    bool prepared_a=memory_boot_m10_task_stack_prepare(0,&a);
    bool prepared_b=prepared_a && memory_boot_m10_task_stack_prepare(1,&b);
    if(prepared_a) d3_stack[0]=(uint8_t *)a;
    if(prepared_b) d3_stack[1]=(uint8_t *)b;
    if(!prepared_b) {
        if(prepared_a) (void)memory_boot_m10_task_stack_release(0);
        d3_failure="ALLOCATE_STACKS";
        d3_irq_restore(entry_flags);
        return false;
    }
    uint16_t code_segment=0,stack_segment=0;
    __asm__ volatile("movw %%cs,%0" : "=r"(code_segment));
    __asm__ volatile("movw %%ss,%0" : "=r"(stack_segment));
    d3_cs=code_segment;
    d3_ss=stack_segment;
    for(size_t i=0;i<D3_WORKERS;++i)
        for(size_t byte=0;byte<D3_CANARY_BYTES;++byte)
            d3_stack[i][byte]=UINT8_C(0xa7);
    bool frames_ready=d3_prepare_frame(0,d3_worker_a) &&
                      d3_prepare_frame(1,d3_worker_b);
    uint64_t start_ms=0,end_ms=0;
    /* M9 publishes elapsed time only after its FIRST hardware tick.
     * At BOOT_OK, the genuine timer is routed but may not have fired yet.
     * The end-of-probe check requires a real tick/time advance. */
    bool clock_started=true;
    (void)clock_ms(&start_ms);
    if(frames_ready && clock_started) {
        d3_running=D3_BOOT;
        d3_active=true;
        d3_irq_restore(entry_flags);
        /* Workers do not yield; the boot stack returns only via IRQ0. */
        while(!d3_done && !d3_failed)
            __asm__ volatile("pause" : : : "memory");
    } else {
        d3_failure=frames_ready ? "CLOCK_PREPARE" : "FRAME_PREPARE";
        d3_irq_restore(entry_flags);
    }
    uint64_t end_flags=d3_irq_disable();
    /* IF=0 prevents further IRQ handoff while examining and releasing. */
    bool valid=frames_ready && clock_started && d3_done && !d3_failed &&
        d3_running==D3_BOOT && d3_switches>=D3_REQUIRED_SLICES*2u &&
        d3_slices[D3_WORKER_A]>=D3_REQUIRED_SLICES &&
        d3_slices[D3_WORKER_B]>=D3_REQUIRED_SLICES &&
        d3_iterations[0]!=0 && d3_iterations[1]!=0 &&
        clock_ms(&end_ms) && end_ms>start_ms;
    for(size_t i=0;i<D3_WORKERS;++i)
        for(size_t byte=0;byte<D3_CANARY_BYTES;++byte)
            if(d3_stack[i][byte]!=UINT8_C(0xa7)) valid=false;
    bool released_b=memory_boot_m10_task_stack_release(1);
    bool released_a=memory_boot_m10_task_stack_release(0);
    d3_irq_restore(end_flags);
    if(!released_a || !released_b) { d3_failure="STACK_RELEASE"; return false; }
    if(!valid && d3_failure[0]=='U') d3_failure="PREEMPTION_PROGRESS";
    return valid;
}
#endif
