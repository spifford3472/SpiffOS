/* Purpose: C2b Phase 0 exact-generation mode-specific IRQ-return gate.
 * Responsibilities: reject cooperative RET continuations as IRETQ targets
 * even after M10-A wakes a mutex waiter; preserve sole task authority.
 * Dependencies: M10-A/D5.2b/D5.3, C1 real IRQ frame metadata validator.
 * Invariants: read-only; no frame fabrication, scheduler mutation, allocation
 * or assumption that an input flag authenticates actual hardware origin.
 * Concurrency: caller must own IF=0 serialized boot CPU IRQ0 context.
 * References: ADR-m10-d58c-c2b-context-modes.md, AGENTS.md. */
#ifndef SPIFFOS_M10_D58C_C2B_RESUME_GATE_H
#define SPIFFOS_M10_D58C_C2B_RESUME_GATE_H
#include "m10-d58c-irq-frame.h"

/* This is a classification of an existing physically owned CPU context,
 * never authority to fabricate one or to change M10-A RUNNING state. */
enum d58c_context_form {
    D58C_CONTEXT_INVALID=0,
    D58C_CONTEXT_COOPERATIVE_RET=1,
    D58C_CONTEXT_VERIFIED_IRQ_IRETQ=2
};

/* An admission descriptor binds the mode and saved-frame address to an
 * exact M10-A generation. C1 must still authenticate all frame bytes. */
struct d58c_context_view {
    struct kernel_task_handle handle;
    enum d58c_context_form form;
    const uint64_t *saved_irq_frame;
};

/* The hardware IRQ0 bridge must supply a real source frame and live IF=0;
 * callers may not promote an arbitrary READY RET waiter into the IRQ lane.
 * Returns false, without any mutation, on missing/stale modes, frame
 * ownership, unsafe state, guard depth or corrupted graph.
 * This helper NEVER writes a CPU context, IRETQ frame or task table. */
bool d58c_c2b_irq_target_eligible(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,const struct d53_sync *sync,
    struct kernel_task_handle source,const uint64_t *source_irq_frame,
    const struct d58c_context_view *target,
    uint16_t expected_cs,uint16_t expected_ss,
    bool real_irq_exit,bool live_if_disabled,bool guard_clear);
#endif
