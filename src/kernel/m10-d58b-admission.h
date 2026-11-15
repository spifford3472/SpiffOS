/* Purpose: D5.8b Phase 1 read-only task graph admission.
 * Responsibilities: distinguish legitimate registered lock ownership from
 * broken D5.3 resource ledgers before a separate IRQ frame validation.
 * Dependencies: M10-A task table, D5.2b ledger, D5.3 lock graph.
 * Invariants: this is never CPU stack/IRQ frame authentication and does
 * not authorize dispatch or mutation; old D5.6 frame gate unchanged.
 * Concurrency: single CPU, caller supplies trusted IF/IRQ provenance.
 * Reference: ADR-m10-d58b-frame-ownership-gate.md and AGENTS.md. */
#ifndef SPIFFOS_M10_D58B_ADMISSION_H
#define SPIFFOS_M10_D58B_ADMISSION_H
#include "kernel-sync-d53.h"

/* True only for a live exact-generation RUNNING/RUNNABLE ordinary task
 * whose lock ownership matches every bounded D5.3 lock and ledger count.
 * The trusted bridge must STILL check full source/target x86 frame geometry,
 * stack canaries, live IF, guard depth and actual interrupt provenance.
 * Returns false without modifying any caller-owned state. */
bool d58b_lock_frame_eligible(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d53_sync *sync,struct kernel_task_handle task,
    enum kernel_task_state required,bool irq_exit,bool live_if_disabled,
    bool saved_if_enabled,bool guard_clear);

/* Metadata-only post-BLOCK witness for a non-IRQ IF=0 dispatcher path.
 * Proves policy transition and outstanding waiter reference, NOT the CPU's
 * actual RSP or context transfer. Caller must independently switch stacks
 * and validate surviving physical context before execution continues. */
bool d58b_blocked_departure_metadata(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d53_sync *sync,struct kernel_task_handle blocked,
    bool in_irq,bool live_if_disabled);
#endif
