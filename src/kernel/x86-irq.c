/* Purpose: bounded PIC interrupt dispatch and dedicated PIT channel-0 setup.
 * Dependencies: x86-irq.h, x86-pic.h, kernel-timer.h and PC-compatible ports.
 * Invariants: timer IRQ0 only is unmasked; no allocations, serial writes,
 * keyboard servicing, blocking waits, or nested interrupts inside handlers.
 * Reference: docs/decisions/ADR-m9-3-irq0-delivery.md. */
#include "x86-irq.h"
#include "x86-pic.h"
#include "kernel-timer.h"
#include <stdint.h>

/* PIT oscillator is 1,193,182 Hz; divisor 11,932 yields approximately
 * 99.9985 Hz. Port 0x43 is PIT command, 0x40 is channel-0 data.
 * Mode 2 reloads periodically and does not touch PIT channel 2/F12. */
enum {
    PIT_COMMAND=0x43, PIT_CHANNEL0=0x40,
    PIT0_MODE2_LOW_HIGH=0x34, PIT0_DIVISOR=11932,
    IRQ_TIMER=0, IRQ_CASCADE=2
};
static bool pit0_started;
static uint32_t unexpected_irqs;

/* Bounded interrupt context: observe one hardware tick and issue one EOI.
 * Unexpected sources are masked where possible and acknowledged; never
 * print on serial, allocate or poll from an ISR. */
void x86_irq_dispatch(uint64_t irq)
{
    if (irq >= X86_PIC_IRQ_COUNT) return;
    if (irq == IRQ_TIMER) {
        (void)kernel_timer_boot_tick();
    } else {
        uint32_t prior = __atomic_load_n(&unexpected_irqs, __ATOMIC_RELAXED);
        if (prior != UINT32_MAX)
            __atomic_store_n(&unexpected_irqs, prior + 1u, __ATOMIC_RELAXED);
        if (irq != IRQ_CASCADE) (void)x86_pic_mask_hardware_irq((uint8_t)irq);
    }
    (void)x86_pic_acknowledge_hardware((uint8_t)irq);
}

/* Reject activation unless IF=0 and the legacy PIC is ready. Program one
 * PIT source, then unmask only IRQ0, leaving IRQ1 keyboard and all slave
 * lines fully masked. Never issue STI or reconfigure PIT channel 2 here. */
bool x86_irq_activate_pit0(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    if (pit0_started || (flags & (1u << 9)) != 0 ||
        !x86_pic_hardware_ready()) return false;
    const uint8_t lo = PIT0_DIVISOR & 0xff;
    const uint8_t hi = (PIT0_DIVISOR >> 8) & 0xff;
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)PIT0_MODE2_LOW_HIGH),
                      "Nd"((uint16_t)PIT_COMMAND) : "memory");
    __asm__ volatile ("outb %0, %1" : : "a"(lo),
                      "Nd"((uint16_t)PIT_CHANNEL0) : "memory");
    __asm__ volatile ("outb %0, %1" : : "a"(hi),
                      "Nd"((uint16_t)PIT_CHANNEL0) : "memory");
    if (!x86_pic_unmask_hardware_irq(IRQ_TIMER)) return false;
    pit0_started = true;
    return true;
}

/* Atomicity requirement: boot CPU may read while ISR updates.
 * Single-core x86 32-bit aligned atomic operations are sufficient; use
 * explicit builtin for readability and future design validation. */
uint32_t x86_irq_unexpected_count(void)
{
    return __atomic_load_n(&unexpected_irqs, __ATOMIC_RELAXED);
}
