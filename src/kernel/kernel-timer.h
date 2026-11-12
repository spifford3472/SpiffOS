/* Purpose: define a bounded monotonic tick service for the boot CPU.
 * Dependencies: C11 integers and compiler 64-bit atomic intrinsics.
 * Invariants: no wall-clock time, heap, scheduler or hardware IO; unavailable
 * until first tick; saturates rather than wrapping. Not a public user ABI.
 * Reference: docs/decisions/ADR-m9-3-monotonic-clock.md. */
#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H
#include <stdbool.h>
#include <stdint.h>

/* Caller-owned state must be zero-initialized before first use. The count
 * is accessed atomically because a hardware IRQ may interrupt a reader. */
struct kernel_timer {
    uint64_t ticks;
    uint32_t ticks_per_second;
    bool initialized;
    bool available;
};

/* Initialize once with positive frequency in ticks per second. Failure
 * returns false without changing caller-owned state or touching hardware. */
bool kernel_timer_init(struct kernel_timer *timer, uint32_t frequency_hz);

/* Record one validated hardware tick, with UINT64_MAX saturation.
 * Returns false if not initialized. Suitable for a bounded IRQ handler. */
bool kernel_timer_note_tick(struct kernel_timer *timer);

/* Read a nondecreasing count only after one tick was observed; false leaves
 * the caller-owned output unmodified. No lock, allocation or busy waiting. */
bool kernel_timer_read_ticks(const struct kernel_timer *timer, uint64_t *out);

/* Convert a nonnegative tick interval to whole milliseconds using quotient
 * and remainder arithmetic. Saturates on overflow; false leaves out alone. */
bool kernel_timer_ticks_to_ms(uint64_t ticks, uint32_t frequency_hz,
                               uint64_t *out);

/* Calculate elapsed ticks between ordered monotonic samples. End before start
 * is invalid; no modular wraparound is inferred. False leaves out unchanged. */
bool kernel_timer_elapsed_ticks(uint64_t start, uint64_t end, uint64_t *out);

/* Single-boot-CPU instance; initialized before enabling any external IRQ.
 * Unavailable until the first Phase D hardware tick is delivered. */
bool kernel_timer_initialize_boot(uint32_t frequency_hz);
bool kernel_timer_boot_tick(void);
bool kernel_timer_boot_now_ms(uint64_t *out);
#endif
