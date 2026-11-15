/* Purpose: D5.8c C1 lock-aware authentic IRQ frame read-only admission.
 * Responsibilities: combine D5.8b whole D5.3 graph/owner metadata with
 * independent full 20-qword x86-64 interrupt frame and stack validation.
 * Dependencies: M10-A, D5.2b/D5.3, D5.8b, M10-D2 IRQ frame ABI.
 * Invariants: no state mutation, EOI, memory access outside stack bounds,
 * allocations or speculative repair; cannot authenticate caller provenance.
 * Concurrency: one boot CPU, verified IRQ0 stack and live IF=0 supplied by
 * the trusted assembly bridge; no normal kernel production integration.
 * Reference: ADR-m10-d58c-real-irq-lock-owner.md and AGENTS.md. */
#ifndef SPIFFOS_M10_D58C_IRQ_FRAME_H
#define SPIFFOS_M10_D58C_IRQ_FRAME_H
#include "m10-d58b-admission.h"
#include <stdint.h>
/* Validate one genuine source RUNNING or queued RUNNABLE target frame.
 * The trusted IRQ bridge must attest that frame came from real IRQ0 or a
 * prevalidated owned-stack IRETQ saved context, with IF disabled right now.
 * Return false before dereferencing any word when span fails. */
bool d58c_irq_frame_eligible(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d53_sync *sync,struct kernel_task_handle identity,
    enum kernel_task_state required,const uint64_t *frame,
    uint16_t expected_cs,uint16_t expected_ss,
    bool irq_exit,bool live_if_disabled,bool guard_clear);
#endif
