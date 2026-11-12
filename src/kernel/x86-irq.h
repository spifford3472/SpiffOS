/* Purpose: define boot-CPU PIC IRQ dispatch and PIT0 activation.
 * Dependencies: PIC and kernel timer internal services, x86-64 IO/IDT.
 * Invariants: only IRQ0 is unmasked; no allocations, scheduler, or blocking
 * work from interrupt context. Only valid in QEMU-verified PIC mode.
 * Reference: docs/decisions/ADR-m9-3-irq0-delivery.md. */
#ifndef X86_IRQ_H
#define X86_IRQ_H
#include <stdbool.h>
#include <stdint.h>
#define X86_PIC_IRQ_COUNT 16

/* Assembly calls with IRQ index 0..15; all interrupted GPRs preserved by
 * the trampoline and RSP aligned before C invocation. */
void x86_irq_dispatch(uint64_t irq);

/* One boot-CPU startup path: program only PIT0 channel 0 for about 100Hz
 * and unmask IRQ0 while IF is still clear. Does not execute STI. */
bool x86_irq_activate_pit0(void);

/* Diagnostic count of unexpected PIC vectors; saturates without writes
 * from normal code. No IRQs other than timer may be unmasked. */
uint32_t x86_irq_unexpected_count(void);
#endif
