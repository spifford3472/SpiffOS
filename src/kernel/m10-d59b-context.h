/* Purpose: bind D5.7 exclusion to actual boot-CPU execution.
 * Dependencies: M10-A/D5.2b owned stacks, D5.7 guard, x86 IRQ entry.
 * Invariants: single CPU; sticky failure retains IF=0; no bootstrap bypass.
 * References: ADR-m10-d59b-authentic-context.md and AGENTS.md. */
#ifndef SPIFFOS_M10_D59B_CONTEXT_H
#define SPIFFOS_M10_D59B_CONTEXT_H
#if defined(SPIFFOS_D59B_HOSTED) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ != 1)
#error Hosted CPU injection is forbidden in freestanding kernel builds
#endif
#include "m10-d57-safety.h"
#include "kernel-task-resources.h"
#ifdef SPIFFOS_D59B_HOSTED
/* Test-owned CPU substitution; compiled out of kernel images entirely. */
struct d59b_host_backend {
    uint64_t (*save_disable)(void *);
    uint64_t (*flags)(void *);
    uint32_t (*cpu)(void *);
    uintptr_t (*rsp)(void *);
    void (*enable)(void *);
    void *opaque;
};
#endif
/* Borrow all state for adapter lifetime. Only D5.7 owns depth/cookies/fault. */
struct d59b_context {
    bool initialized;
    bool original_if;
    uint32_t expected_cpu;
    /* Diagnostic TSC samples, not calibrated time or scheduler authority. */
    uint64_t entry_tsc;
    uint64_t max_exclusion_tsc;
    uint64_t completed_exclusions;
    struct d57_preempt_guard *guard;
    const struct kernel_task_table *tasks;
    const struct kernel_task_resources *resources;
#ifdef SPIFFOS_D59B_HOSTED
    const struct d59b_host_backend *host;
#endif
};
/* Actual flagged IRQ entry/exit writes this word with IF disabled. */
extern volatile uint32_t d59b_irq_depth;
/* Bind zeroed adapter once, IF=0/non-IRQ, to healthy borrowed guard/state.
 * No guard reset, ownership transfer, allocation or CPU switch. */
bool d59b_context_init(struct d59b_context *context,
    struct d57_preempt_guard *guard,const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources);
/* Capture original IF then CLI; authenticate actual CPU/RSP/task/IRQ.
 * Publish cookie only after success; failure retains IF=0 and sticky fault.
 * Never perform allocator work after rejection. Caller owns output storage. */
bool d59b_enter(struct d59b_context *context,uint32_t *cookie);
/* Exact RUNNING owner releases top D5.7 cookie. Only successful final exit
 * may restore original IF. Failure retains exclusion; never force-unlock. */
bool d59b_exit(struct d59b_context *context,uint32_t cookie);
#endif
