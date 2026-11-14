/* Purpose: D5.5b test-only x86-64 IRQ0 frame provenance verification.
 * Responsibilities: cross-check CPU-saved trap frame against independent
 * assembly sentinel and actual IF=0 observed in the IRQ C hook.
 * Dependencies: M10-D2 20-qword interrupt frame and assembly probe runner.
 * Invariants: never mutate saved frame, schedule, log, allocate or EOI.
 * Concurrency: one boot CPU; IRQ hook runs IF=0, never nested in QEMU.
 * References: ADR-m10-d55b-x86-evidence.md and AGENTS.md. */
#ifndef SPIFFOS_M10_D55B_EVIDENCE_H
#define SPIFFOS_M10_D55B_EVIDENCE_H
#include <stdbool.h>
#include <stdint.h>

/* IRQ hook called after the existing M9 timer/EOI, before IRETQ. No
 * frame pointer from a caller outside the actual entry assembly. */
void m10_d55b_observe_irq(const uint64_t *frame);

/* Query only after M10-D2's independent probe has returned to boot. */
bool m10_d55b_evidence_passed(void);
#endif
