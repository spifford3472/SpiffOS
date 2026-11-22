/* Purpose: C2b Phase 0 immutable RET-vs-IRETQ IRQ handoff admission.
 * Responsibilities: ensure a target READY task has an exact-generation,
 * independently verified full IRETQ context instead of a parked RET stack.
 * Dependencies: m10-d58c-c2b-resume-gate.h and C1 full frame checker.
 * Invariants: no state, frame, lock, EOI or context-format mutation.
 * Concurrency: one CPU, caller serializes and owns authentic IRQ evidence.
 * References: ADR-m10-d58c-c2b-context-modes.md, AGENTS.md. */
#include "m10-d58c-c2b-resume-gate.h"

/* Source is a true, separately authenticated IRQ0 frame, even when the
 * owner originally entered by cooperative x86-task-switch. A target,
 * unlike the source, MUST already have a complete independent saved
 * IRETQ frame, validated against its exact generation and stack backing.
 * This is a predicate, not an x86 IRQ handoff operation. */
bool d58c_c2b_irq_target_eligible(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,const struct d53_sync *sync,
    struct kernel_task_handle source,const uint64_t *source_irq_frame,
    const struct d58c_context_view *target,
    uint16_t expected_cs,uint16_t expected_ss,
    bool real_irq_exit,bool live_if_disabled,bool guard_clear)
{
    if(!real_irq_exit || !live_if_disabled || !guard_clear ||
       target==NULL || source_irq_frame==NULL ||
       target->form!=D58C_CONTEXT_VERIFIED_IRQ_IRETQ ||
       target->saved_irq_frame==NULL ||
       target->handle.generation==0 ||
       source.slot==target->handle.slot) return false;

    /* A copied/forged context-form descriptor cannot substitute for
     * exact M10-A generation or a true fully owned READY CPU frame. */
    return d58c_irq_frame_eligible(tasks,resources,sync,source,
            KERNEL_TASK_RUNNING,source_irq_frame,expected_cs,expected_ss,
            true,live_if_disabled,guard_clear) &&
        d58c_irq_frame_eligible(tasks,resources,sync,target->handle,
            KERNEL_TASK_RUNNABLE,target->saved_irq_frame,
            expected_cs,expected_ss,true,live_if_disabled,guard_clear);
}
