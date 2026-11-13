/* Purpose: deterministic M10-C scheduler tick and reschedule signal.
 * Dependencies: kernel-scheduler-clock.h; no dependency on task stacks,
 * process state, video, drivers or the M9 monotonic clock's ownership.
 * Invariants: single IRQ writer, saturating u64 ticks, bounded u32 phase,
 * atomic coalescing request flag; only safe-point readers clear requests.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "kernel-scheduler-clock.h"
#include <stddef.h>

/* Initialize IF=0 owned state without publishing partial readiness. */
bool kernel_scheduler_clock_init(struct kernel_scheduler_clock *clock,
                                 uint32_t interval_ticks)
{
    if(clock==NULL || clock->initialized || interval_ticks==0)
        return false;
    clock->tick_count=0;
    clock->phase_ticks=0;
    clock->request_interval_ticks=interval_ticks;
    clock->reschedule_pending=false;
    clock->initialized=true;
    return true;
}

/* The existing PIT0 IRQ is the only writer. Atomic publication allows
 * non-interrupt readers to observe whole ticks and the pending flag.
 * A saturated counter stops generating new fictional time slices. */
bool kernel_scheduler_clock_note_tick(struct kernel_scheduler_clock *clock)
{
    if(clock==NULL || !clock->initialized ||
       clock->request_interval_ticks==0) return false;
    uint64_t ticks=__atomic_load_n(&clock->tick_count,__ATOMIC_RELAXED);
    if(ticks==UINT64_MAX) return true;
    __atomic_store_n(&clock->tick_count,ticks+1u,__ATOMIC_RELAXED);
    uint32_t next=clock->phase_ticks+1u;
    /* phase is always < interval; avoid wrap even for UINT32_MAX. */
    if(next>=clock->request_interval_ticks || next==0) {
        clock->phase_ticks=0;
        __atomic_store_n(&clock->reschedule_pending,true,__ATOMIC_RELEASE);
    } else {
        clock->phase_ticks=next;
    }
    return true;
}

/* Reads can be interrupted, but an aligned x86-64 u64 atomic never tears. */
bool kernel_scheduler_clock_read_ticks(
    const struct kernel_scheduler_clock *clock, uint64_t *out)
{
    if(clock==NULL || out==NULL || !clock->initialized) return false;
    *out=__atomic_load_n(&clock->tick_count,__ATOMIC_RELAXED);
    return true;
}

/* Consume a Boolean request, never a task descriptor or interrupt frame.
 * x86 atomic exchange prevents a concurrently published IRQ request from
 * being overwritten by a separate load/store clear pair. */
bool kernel_scheduler_clock_take_request(struct kernel_scheduler_clock *clock)
{
    if(clock==NULL || !clock->initialized) return false;
    return __atomic_exchange_n(&clock->reschedule_pending,false,
                               __ATOMIC_ACQ_REL);
}

/* Static scheduler clock is independent from the M9 clock and retains
 * no pointers into workspace or interrupt stacks. */
static struct kernel_scheduler_clock boot_scheduler_clock;

/* Startup-only adapter, invoked while hardware IRQs remain disabled. */
bool kernel_scheduler_clock_boot_init(uint32_t interval_ticks)
{
    return kernel_scheduler_clock_init(&boot_scheduler_clock,interval_ticks);
}

/* IRQ adapter does not acknowledge LAPIC: M9 handler retains EOI ownership. */
bool kernel_scheduler_clock_boot_note_tick(void)
{
    return kernel_scheduler_clock_note_tick(&boot_scheduler_clock);
}

/* A safe cooperative caller can consume this bit without switching in IRQ0. */
bool kernel_scheduler_clock_boot_take_request(void)
{
    return kernel_scheduler_clock_take_request(&boot_scheduler_clock);
}

/* Read-only sample for tests and future scheduler instrumentation. */
bool kernel_scheduler_clock_boot_read_ticks(uint64_t *out)
{
    return kernel_scheduler_clock_read_ticks(&boot_scheduler_clock,out);
}
