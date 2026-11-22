/* Purpose: M10-D5.5c read-only authoritative task IRQ-frame binding.
 * Responsibilities: validate saved 20-qword hardware frame against the
 * RUNNING exact-generation M10-A task and D5.2b registered stack owner;
 * measure completed supervisor service using independent monotonic IRQ ticks.
 * Dependencies: kernel-task-resources.h and M10-D2 IRQ frame description.
 * Invariants: never schedules, changes the task table, dereferences an
 * unchecked frame or releases owner resources. Test-only, not a public ABI.
 * Concurrency: single boot CPU, trusted IRQ entry captures evidence; no
 * allocation, blocking or critical-section modification in this module.
 * References: ADR-m10-d55c-task-frame-budget.md, AGENTS.md. */
#ifndef SPIFFOS_M10_D55C_FRAME_OWNER_H
#define SPIFFOS_M10_D55C_FRAME_OWNER_H
#include "kernel-task-resources.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Value from trusted CPU assembly is mandatory in real use. These arguments
 * are UNTRUSTED in hosted tests; passing the policy cannot authenticate them. */
struct d55c_irq_snapshot {
    struct kernel_task_handle current;
    const uint64_t *saved_frame;
    uint64_t expected_cs;
    uint64_t expected_ss;
    bool cpu_if_clear;
    unsigned irq_depth;
    unsigned critical_depth;
};

/* Fully check exact M10-A RUNNING descriptor + D5.2b frame owner before
 * reading any saved frame word. No state changes even on success. */
bool d55c_frame_matches(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d55c_irq_snapshot *evidence);

/* Hardware tick service monitor: it never receives a dispatch count.
 * Requires explicit successful supervisor service completion to reset. */
struct d55c_tick_service {
    bool initialized;
    bool faulted;
    uint64_t last_tick;
    uint64_t last_completed_service_tick;
    uint64_t service_completions;
    uint64_t deadline_ticks;
    uint64_t max_observed_gap_ticks;
};

/* Initialize unused caller-zeroed state; one CPU and no time source lookup. */
bool d55c_tick_init(struct d55c_tick_service *watch,
    uint64_t initial_tick,uint64_t deadline_ticks);

/* Latch a missed deadline, invalid sample or backward time. */
bool d55c_tick_observe(struct d55c_tick_service *watch,
    bool sample_valid,uint64_t hardware_tick);

/* Only trusted supervisor execution completion (not selection) resets timer.
 * False leaves last service unchanged and never clears a prior fault. */
bool d55c_tick_service_completed(struct d55c_tick_service *watch,
    bool verified_supervisor_context,bool sample_valid,uint64_t hardware_tick);
#endif
