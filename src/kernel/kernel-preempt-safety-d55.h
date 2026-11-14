/* Purpose: D5.5 conservative execution admission and service deadline monitor.
 * Responsibilities: reject unsafe entry contexts, stale generation handles,
 * in-IRQ resource mutations and supervisor starvation observations.
 * Dependencies: M10-A task state and D5.2b registered stack resources.
 * Invariants: NEVER switches CPU context, touches hardware, frees backing
 * or mutates task/lock metadata; fail-closed and bounded with no allocations.
 * Concurrency: one boot CPU; caller owns serialization and true CPU evidence.
 * References: ADR-m10-d55-preemption-safety.md, AGENTS.md. */
#ifndef SPIFFOS_KERNEL_PREEMPT_SAFETY_D55_H
#define SPIFFOS_KERNEL_PREEMPT_SAFETY_D55_H

#include "kernel-task-resources.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read-only evidence must come from a future trusted assembly/dispatcher
 * adapter. An arbitrary caller supplying a forged snapshot is NOT trusted. */
enum d55_entry_origin {
    D55_ORIGIN_COOPERATIVE=1,
    D55_ORIGIN_IRQ_RETURN=2,
    D55_ORIGIN_DISPATCHER=3
};
enum d55_operation {
    D55_OP_SWITCH=1,
    D55_OP_SYNC_EDIT=2,
    D55_OP_CANCEL=3,
    D55_OP_REAP=4
};
enum d55_result {
    D55_ALLOWED,
    D55_DENIED,
    D55_MONITOR_FAULT
};
struct d55_cpu_evidence {
    enum d55_entry_origin origin;
    uint32_t irq_depth;
    uint32_t critical_depth;
    bool interrupts_enabled;
    bool on_dispatcher_stack;
    uintptr_t actual_rsp;
    struct kernel_task_handle current;
};

/* At boot, the trusted supervisor establishes clock and nonzero deadline.
 * A mere task selection is NOT service completion. A fault is sticky. */
struct d55_service_monitor {
    bool initialized;
    bool faulted;
    uint64_t deadline_ms;
    uint64_t last_sample_ms;
    uint64_t last_service_ms;
    uint64_t verified_services;
};

/* No host clock calls: snapshot comes from a trusted monotonic source. */
bool d55_monitor_init(struct d55_service_monitor *monitor,
    uint64_t now_ms,uint64_t deadline_ms);

/* Detect missed wall-clock service and clock rollback, latching a fault.
 * Exactly equal deadline is allowed; more than deadline is a fault. */
enum d55_result d55_monitor_sample(struct d55_service_monitor *monitor,
    bool clock_valid,uint64_t now_ms);

/* Mark a verified supervisor service completion only after executing on
 * the surviving dispatcher/supervisor stack, IF=0 and outside IRQ. */
enum d55_result d55_monitor_service(struct d55_service_monitor *monitor,
    const struct d55_cpu_evidence *evidence,
    const struct kernel_task_table *table,
    const struct kernel_task_resources *resources,
    bool clock_valid,uint64_t now_ms);

/* Pure admission: task and resource tables remain unchanged on all results.
 * An interrupt-return switch requires exactly one IRQ, IF=0, no active
 * critical section, exact running handle and no owned lock/wait ref.
 * Cooperative handoff permits owned locks but forbids nested critical edits.
 * SYNC edits require exact running worker IF=0 outside IRQ.
 * CANCEL and REAP require surviving boot/supervisor context, IF=0,
 * off every registered task stack and outside any IRQ/critical section.
 * The monitor must have a valid, non-expired supervisor service sample. */
enum d55_result d55_admit(const struct d55_service_monitor *monitor,
    const struct d55_cpu_evidence *evidence,enum d55_operation operation,
    const struct kernel_task_table *table,
    const struct kernel_task_resources *resources);
#endif
