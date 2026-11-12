/* Purpose: deterministic saturating monotonic kernel tick service.
 * Dependencies: kernel-timer.h and x86-64 lock-free aligned u64 atomics.
 * Invariants: one boot CPU, no IO or allocation; first IRQ makes time
 * available; no wall-clock semantics or preemptive scheduler.
 * Reference: docs/decisions/ADR-m9-3-monotonic-clock.md. */
#include "kernel-timer.h"
#include <stddef.h>
#include <stdint.h>

/* Initialize caller-owned state once. Reinitialization could move elapsed
 * time backwards, so it is rejected. This is startup-only with IRQs off. */
bool kernel_timer_init(struct kernel_timer *timer, uint32_t frequency_hz)
{
    if (timer == NULL || timer->initialized || frequency_hz == 0)
        return false;
    timer->ticks = 0;
    timer->ticks_per_second = frequency_hz;
    timer->available = false;
    timer->initialized = true;
    return true;
}

/* Single-CPU ISR increment. The load/store are atomic so preempted readers
 * observe a whole uint64_t. ISR cannot interrupt itself while using an x86
 * interrupt gate. UINT64_MAX is terminal to avoid elapsed-time regression. */
bool kernel_timer_note_tick(struct kernel_timer *timer)
{
    if (timer == NULL || !timer->initialized) return false;
    uint64_t current = __atomic_load_n(&timer->ticks, __ATOMIC_RELAXED);
    if (current < UINT64_MAX)
        __atomic_store_n(&timer->ticks, current + 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&timer->available, true, __ATOMIC_RELEASE);
    return true;
}

/* Do not invent zero elapsed time before the hardware clock is live.
 * Acquire observes availability and its prior tick publication. */
bool kernel_timer_read_ticks(const struct kernel_timer *timer, uint64_t *out)
{
    if (timer == NULL || out == NULL || !timer->initialized ||
        !__atomic_load_n(&timer->available, __ATOMIC_ACQUIRE))
        return false;
    *out = __atomic_load_n(&timer->ticks, __ATOMIC_RELAXED);
    return true;
}

/* Quotient/remainder prevents ticks*1000 overflow and retains integer ms
 * precision. A huge positive value saturates at UINT64_MAX, not modulo 2^64.
 * The remainder multiply cannot overflow: u32 remainder * 1000 < 2^42. */
bool kernel_timer_ticks_to_ms(uint64_t ticks, uint32_t frequency_hz,
                               uint64_t *out)
{
    if (out == NULL || frequency_hz == 0) return false;
    uint64_t whole = ticks / frequency_hz;
    uint64_t fractional = (ticks % frequency_hz) * UINT64_C(1000) /
                           frequency_hz;
    if (whole > (UINT64_MAX - fractional) / UINT64_C(1000))
        *out = UINT64_MAX;
    else
        *out = whole * UINT64_C(1000) + fractional;
    return true;
}

/* Saturating tick counter never wraps, so decreasing samples signal a stale
 * or invalid sample and must not be mistaken for a huge elapsed interval. */
bool kernel_timer_elapsed_ticks(uint64_t start, uint64_t end, uint64_t *out)
{
    if (out == NULL || end < start) return false;
    *out = end - start;
    return true;
}

static struct kernel_timer boot_timer;

/* Initialize the one static boot-CPU timer; no PIT access occurs here. */
bool kernel_timer_initialize_boot(uint32_t frequency_hz)
{
    return kernel_timer_init(&boot_timer, frequency_hz);
}

/* Phase D IRQ0 calls only this bounded function before sending PIC EOI. */
bool kernel_timer_boot_tick(void)
{
    return kernel_timer_note_tick(&boot_timer);
}

/* Translate the live boot timer to milliseconds; false means no hardware
 * tick has arrived or the caller passed an invalid pointer. */
bool kernel_timer_boot_now_ms(uint64_t *out)
{
    uint64_t ticks;
    if (!kernel_timer_read_ticks(&boot_timer, &ticks)) return false;
    return kernel_timer_ticks_to_ms(ticks, boot_timer.ticks_per_second, out);
}
