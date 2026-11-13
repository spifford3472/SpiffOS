/* Purpose: test-only M10-D2 full x86-64 hardware IRQ frame proof.
 * Dependencies: real PIT0/APIC IRQ0 in QEMU and the M9 15-GPR IRQ stub.
 * Invariants: no handoff, no allocations, no locks/IO in IRQ context.
 * Intel SDM vol. 3A 7.14.2: long-mode IRQ pushes SS:RSP even at CPL 0.
 * Reference: docs/decisions/ADR-m10-d-preemption-proposal.md.
 */
#ifndef SPIFFOS_M10_D2_IRQ_FRAME_H
#define SPIFFOS_M10_D2_IRQ_FRAME_H
#include <stdbool.h>
#include <stdint.h>

/* Hardware frame follows fifteen software-saved 64-bit GPRs.
 * The IRQ0 gate has no error code, and uses IST=0 at CPL=0.
 * On entry: 0..14 R15..RAX, 15 RIP,16 CS,17 RFLAGS,18 RSP,19 SS.
 * Only a verified same-CPL test entry is permitted at this checkpoint. */
enum {
    M10_D2_R15=0, M10_D2_R14, M10_D2_R13, M10_D2_R12,
    M10_D2_R11, M10_D2_R10, M10_D2_R9, M10_D2_R8,
    M10_D2_RBP, M10_D2_RDI, M10_D2_RSI, M10_D2_RDX,
    M10_D2_RCX, M10_D2_RBX, M10_D2_RAX,
    M10_D2_RIP, M10_D2_CS, M10_D2_RFLAGS, M10_D2_RSP,
    M10_D2_SS, M10_D2_FRAME_QWORDS
};

/* Assembly runner enters a register-sentinel busy loop with IRQs enabled,
 * waiting only for the IRQ checker to mark its result. Called from boot
 * code once IRQ0 is proven available; no interrupt handler calls it. */
void m10_d2_probe_run(void);

/* A test-only C hook called after normal IRQ0 accounting + LAPIC EOI,
 * with the saved GPR frame pointer. Never changes a frame or RFLAGS,
 * never writes serial or schedules from the IRQ. */
void m10_d2_probe_on_irq(const uint64_t *frame);

/* Reports success if >=3 real hardware IRQ0 observations matched all
 * register and hardware-frame invariants. No mutation. */
bool m10_d2_probe_passed(void);
#endif
