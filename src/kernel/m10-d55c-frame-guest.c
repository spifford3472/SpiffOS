/* Purpose: D5.5c real IRQ0 frame proof on one authoritative M10-A worker.
 * Responsibilities: allocate verified owned stack, dispatch exact M10-A
 * generation from boot, observe actual CPU IRQ0 frame, then off-stack reap.
 * Dependencies: M10-A/D5.2b, x86 cooperative context, M2 stack provider,
 * D5.5c read-only frame validator and normal IRQ0 timer delivery.
 * Invariants: no task-state mutation in IRQ; no IRQ-time stack release,
 * no allocation/serial/locking in ISR, one boot CPU and one real worker.
 * Concurrency: state changes only in IF=0 boot/worker safe sections.
 * Reference: ADR-m10-d55c-task-frame-budget.md, AGENTS.md. */
#include "m10-d55c-frame-owner.h"
#include "m10-d2-irq-frame.h"
#include "x86-task-context.h"
#include "memory-boot.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SPIFFOS_M10_D55C_FRAME_GUEST_TEST
enum { D55C_REQUIRED_FRAMES=4 };
static struct kernel_task_table table;
static struct kernel_task_resources resources;
static struct kernel_task_handle task;
static struct x86_task_context boot_context,worker_context;
static uint8_t *worker_stack;
static volatile uint32_t observations;
static volatile bool observation_failed,worker_completed;
static bool guest_active;
static uint64_t expected_cs,expected_ss;
static const char *failure_reason="UNSET";

/* Return stable static label only after switched back to boot context. */
const char *m10_d55c_frame_guest_failure_reason(void)
{
    return failure_reason;
}

/* Save caller flags and mask IRQs during authoritative state/owner edits. */
static uint64_t irq_disable(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    return flags;
}

/* Restore IF precisely as it was before the critical section. */
static void irq_restore(uint64_t flags)
{
    if((flags&UINT64_C(0x200))!=0)
        __asm__ volatile("sti":::"memory");
}

/* The original x86-irq0.S supplies the actual register-save frame after
 * M9 IRQ0 accounting/EOI. No state change, switch or resource release. */
void m10_d55c_frame_irq_observe(const uint64_t *frame)
{
    if(!guest_active || observations>=D55C_REQUIRED_FRAMES ||
       observation_failed) return;
    uint64_t live_flags=0;
    __asm__ volatile("pushfq; popq %0":"=r"(live_flags)::"memory");
    struct d55c_irq_snapshot sample={
        .current=task,
        .saved_frame=frame,
        .expected_cs=expected_cs,
        .expected_ss=expected_ss,
        .cpu_if_clear=(live_flags&UINT64_C(0x200))==0,
        .irq_depth=1,
        .critical_depth=0
    };
#ifdef SPIFFOS_M10_D55C_BAD_GENERATION_TEST
    /* Negative: corrupt only claimed identity, never actual M10-A record
     * or CPU trap frame. Proof must fail before keyboard is enabled. */
    ++sample.current.generation;
#endif
    if(!d55c_frame_matches(&table,&resources,&sample)) {
        observation_failed=true;
        return;
    }
    ++observations;
}

/* Never voluntarily switch until a real timer IRQ has inspected the
 * currently executing worker's authentic saved frame. The only switch
 * back occurs AFTER four proven observations (or a diagnostic failure). */
__attribute__((noreturn)) static void worker_run(void)
{
    volatile uint64_t work=UINT64_C(1);
    while(observations<D55C_REQUIRED_FRAMES && !observation_failed) {
        work=work*UINT64_C(6364136223846793005)+UINT64_C(1);
        __asm__ volatile("pause":::"memory");
    }
    if(work==0) observation_failed=true; /* prevent optimized dead work */
    uint64_t flags=irq_disable();
    if(kernel_task_terminate(&table,task)!=KERNEL_TASK_OK)
        observation_failed=true;
    irq_restore(flags);
    worker_completed=true;
    x86_task_switch(&worker_context,&boot_context);
    for(;;) __asm__ volatile("cli; hlt":::"memory");
}

/* Boot dispatcher owns backing, creates M10-A identity, then yields
 * execution onto exactly that stack. Reaping is OFF that worker stack. */
bool m10_d55c_frame_guest_smoke(bool (*clock_ms)(uint64_t *))
{
    if(clock_ms==NULL) {failure_reason="CLOCK";return false;}
    uint64_t flags=irq_disable();
    if((flags&UINT64_C(0x200))==0) {
        failure_reason="IF_DISABLED";return false;
    }
    void *owned=NULL;
    if(!memory_boot_m10_task_stack_prepare(0,&owned)) {
        irq_restore(flags);failure_reason="STACK_PREPARE";return false;
    }
    worker_stack=(uint8_t *)owned;
    irq_restore(flags);
    bool ready=kernel_task_init(&table) &&
        kernel_task_resources_init(&resources);
    if(!ready) {failure_reason="INIT";goto failure;}
    for(size_t i=0;i<X86_TASK_STACK_BYTES;++i)
        worker_stack[i]=UINT8_C(0x5a);
    if(!x86_task_context_init(&worker_context,worker_stack,
                X86_TASK_STACK_BYTES,worker_run)) {
        failure_reason="CONTEXT";goto failure;
    }
    uint16_t cs=0,ss=0;
    __asm__ volatile("movw %%cs,%0":"=r"(cs));
    __asm__ volatile("movw %%ss,%0":"=r"(ss));
    expected_cs=cs;expected_ss=ss;
    flags=irq_disable();
    struct kernel_task_stack_binding binding={
        .bottom=(uintptr_t)worker_stack,
        .bytes=X86_TASK_STACK_BYTES,
        .saved_rsp=(uintptr_t)worker_context.saved_rsp,
        .owner_index=0
    };
    struct kernel_task_handle selected={0};
    ready=kernel_task_create(&table,false,&task)==KERNEL_TASK_OK &&
        kernel_task_resources_bind(&resources,&table,task,&binding)==
            KERNEL_RESOURCE_OK &&
        kernel_task_make_runnable(&table,task)==KERNEL_TASK_OK &&
        kernel_task_dispatch(&table,&selected)==KERNEL_TASK_OK &&
        selected.slot==task.slot &&
        selected.generation==task.generation;
    irq_restore(flags);
    if(!ready) {failure_reason="TASK_BIND";goto failure;}
    uint64_t start=0,finish=0;
    bool clock=clock_ms(&start);
    guest_active=true;
    x86_task_switch(&boot_context,&worker_context);
    guest_active=false;
    /* Worker is now TERMINATED and its stack cannot be running. */
    flags=irq_disable();
    uintptr_t actual_rsp;
    __asm__ volatile("mov %%rsp,%0":"=r"(actual_rsp)::"memory");
    enum kernel_task_resource_result saved=
        kernel_task_resources_saved_rsp(&resources,&table,task,
            (uintptr_t)worker_context.saved_rsp);
    bool good=worker_completed && !observation_failed &&
        observations==D55C_REQUIRED_FRAMES &&
        saved==KERNEL_RESOURCE_OK &&
        kernel_task_check(&table) &&
        kernel_task_resources_check(&resources,&table) &&
        clock && clock_ms(&finish) && finish>start;
    for(size_t i=0;i<64;++i)
        if(worker_stack[i]!=UINT8_C(0x5a)) good=false;
    enum kernel_task_resource_result retired=kernel_task_resources_retire(
        &resources,&table,task,actual_rsp,
        memory_boot_m10_task_stack_release);
    irq_restore(flags);
    if(retired!=KERNEL_RESOURCE_OK) {
        failure_reason="REAPER";return false;
    }
    if(!good) {
        failure_reason="ACTUAL_FRAME_OR_CLOCK";
        return false;
    }
    return true;
failure:
    flags=irq_disable();
    (void)memory_boot_m10_task_stack_release(0);
    irq_restore(flags);
    return false;
}
#endif
