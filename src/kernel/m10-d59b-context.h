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
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
/* Internal structured irq-save token. Runtime cookies belong ONLY to D5.7.
 * Zero-initialize before entry; never copy, forge or retain past matching exit. */
struct d59b_memory_scope {
    struct d59b_context *context;
    uint64_t previous_flags;
    uint32_t cookie;
    bool active;
};
#ifdef SPIFFOS_D59B_HOSTED
/* Install native CPU samples before bootstrap ONLY; absent from kernel. */
bool d59b_memory_host_backend(const struct d59b_host_backend *backend);
#endif
/* Once-only real boot CPU initialization, IF=0/off IRQ before allocator use. */
bool d59b_memory_bootstrap(void);
/* Bind once from surviving boot stack after reserved SUP dispatch, IF=0. */
bool d59b_memory_bind(struct d59b_context *context);
/* Disarm handoff first; require no RUNNING task and actual RSP off all stacks. */
bool d59b_memory_unbind(struct d59b_context *context);
/* Clear borrowed retired context only after ALL installed stacks are gone. */
bool d59b_memory_retirement_complete(struct d59b_context *context);
/* Read-only selector predicate; caller already owns real IRQ exclusion. */
bool d59b_memory_is_bound(const struct d59b_context *context);
/* Internal allocator wrappers enter before shared metadata inspection.
 * Success publishes token; refusal preserves token/output and retains IF=0. */
bool d59b_memory_scope_enter(struct d59b_memory_scope *scope);
/* Exact structured exit; only success may restore original IF.
 * Wrappers MUST stop instead of returning after an uncertain exit. */
bool d59b_memory_scope_leave(struct d59b_memory_scope *scope);
/* Query only inside an admitted scope; legacy callbacks/lifecycle stay boot. */
bool d59b_memory_boot_only(void);
/* Bounded boot-API admission check. It releases the check scope before long
 * legacy reports/snapshots: unbound mode has NO independently scheduled task.
 * Runtime returns false before any callback, borrowed output or mutation. */
bool d59b_memory_boot_admit(void);
/* Query existing D5.2b bindings using validated HHDM, under admitted scope.
 * Bound FREE storage is not reusable; no duplicate ownership table exists. */
bool d59b_memory_frame_bound(uint64_t physical);
/* Exact TERMINATED/off-stack/no-ref retirement only; runtime release denied. */
bool d59b_memory_frame_release_allowed(uint64_t physical);
/* Nonreturning IF-off failure; never publish a false recoverable result. */
_Noreturn void d59b_memory_stop(void);
#endif

#endif
