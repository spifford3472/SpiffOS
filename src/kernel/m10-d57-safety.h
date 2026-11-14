/* Purpose: M10-D5.7 single-CPU preempt exclusion and donated-priority
 * selection gates for the existing M10-A IRQ handoff only.
 * Responsibilities: exact generation-guarded, LIFO critical sections;
 * immutable IRQ priority selection using the verified D5.3 lock graph
 * and existing D1 aging/supervisor policy.
 * Dependencies: M10-A, D5.2b resource registry, D5.3 sync, D1 policy.
 * Invariants: never edits M10-A or mutex state; IRQ calls read only and
 * always require verified live IF=0 and authentic CPU frame provenance.
 * Concurrency: caller owns single boot CPU IRQ exclusion throughout.
 * Reference: ADR-m10-d57-preemption-and-priority.md, AGENTS.md. */
#ifndef SPIFFOS_M10_D57_SAFETY_H
#define SPIFFOS_M10_D57_SAFETY_H
#include "kernel-sync-d53.h"
#include <stdbool.h>
#include <stdint.h>

enum { D57_MAX_GUARD_DEPTH=8 };
struct d57_preempt_guard {
    bool initialized;
    bool faulted;
    uint32_t depth;
    struct kernel_task_handle owner;
    uint32_t serial[D57_MAX_GUARD_DEPTH];
    uint32_t next_serial;
};

/* Initialize previously zeroed storage. Never resets a latched fault. */
bool d57_guard_init(struct d57_preempt_guard *guard);

/* Caller verified IF=0, non-IRQ context, exact authoritative RUNNING
 * generation. Out cookie is written only after a successful push. */
bool d57_guard_enter(struct d57_preempt_guard *guard,
    struct kernel_task_handle owner,bool in_irq,bool live_if_clear,
    uint32_t *cookie);

/* Require exact top cookie, owner and disabled IRQs for proper LIFO pop.
 * Any violated preempt contract latches fault for lifetime of guard. */
bool d57_guard_exit(struct d57_preempt_guard *guard,
    struct kernel_task_handle owner,bool in_irq,bool live_if_clear,
    uint32_t cookie);

/* Policy gating only. Saved IF is from actual hardware frame, live IF is
 * from trusted IRQ handler. No allocation, context switch or mutation. */
bool d57_guard_may_switch(const struct d57_preempt_guard *guard,
    bool irq_exit,bool saved_if_enabled,bool live_if_disabled);

/* Priority state is separate from M10-A and cannot manufacture task
 * lifecycle. It tracks only ages/identities and validated decision count. */
struct d57_irq_policy {
    bool initialized;
    uint64_t decisions;
    uint64_t age[KERNEL_TASK_CAPACITY];
    struct kernel_task_handle generation[KERNEL_TASK_CAPACITY];
};

bool d57_policy_init(struct d57_irq_policy *policy);

/* Compute an exact RUNNABLE target from M10-A ready queue using D5.3
 * transitive effective priorities and D1 supervisor reservation. Reject
 * every invalid graph/handle/owner without changing policy/output.
 * No D5.3 mutation, M10-A state transition, frame dereference or I/O. */
bool d57_select(const struct d57_irq_policy *policy,
    const struct d53_sync *sync,
    const struct kernel_task_resources *resources,
    const struct kernel_task_table *tasks,
    struct kernel_task_handle *out);

/* Record ages ONLY after caller has successfully validated both saved
 * frames and M10-A committed its exact IRQ handoff. No rollback by policy. */
bool d57_note_handoff(struct d57_irq_policy *policy,
    const struct kernel_task_table *tasks,
    struct kernel_task_handle selected);
#endif
