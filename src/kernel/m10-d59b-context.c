/* Purpose: authenticate privileged execution before using the D5.7 guard.
 * Dependencies: real x86-64 flags/APIC ID/RSP and IRQ0 depth, M10-A/D5.2b.
 * Invariants: no caller-provided task/IRQ/IF witness in the kernel build;
 * no second lock, scheduler, allocation or force-release mechanism.
 * References: ADR-m10-d59b-authentic-context.md and AGENTS.md.
 * Provenance: Qwen current-owner predicate, coordinator CPU/backend glue;
 * admission/exit replaced by narrowly scoped Codex fallback after three
 * failed Qwen core attempts; see the operational evidence report. */
#include "m10-d59b-context.h"
#define D59B_IF_MASK UINT64_C(0x200)
volatile uint32_t d59b_irq_depth;

/* Validate hosted callbacks before invoking them; kernel has no backend. */
static bool backend_valid(const struct d59b_context *context)
{
#ifdef SPIFFOS_D59B_HOSTED
    return context!=NULL && context->host!=NULL &&
        context->host->save_disable!=NULL && context->host->flags!=NULL &&
        context->host->cpu!=NULL && context->host->rsp!=NULL &&
        context->host->enable!=NULL;
#else
    return context!=NULL;
#endif
}
/* Sample architectural IF without altering it. Memory clobber prevents
 * compiler movement of shared metadata loads across the observation. */
static uint64_t cpu_flags(const struct d59b_context *context)
{
#ifdef SPIFFOS_D59B_HOSTED
    return context->host->flags(context->host->opaque);
#else
    (void)context;
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0":"=r"(flags)::"memory");
    return flags;
#endif
}
/* Capture original flags then CLI in one compiler-visible sequence.
 * An interrupt between PUSHFQ and CLI may finish normally before entry;
 * no guarded metadata has been inspected yet, and saved IF stays exact. */
static uint64_t cpu_save_disable(const struct d59b_context *context)
{
#ifdef SPIFFOS_D59B_HOSTED
    return context->host->save_disable(context->host->opaque);
#else
    (void)context;
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    return flags;
#endif
}
/* Read legacy initial APIC identity from CPUID leaf 1. This is scoped to
 * one boot CPU; it is not an SMP identity scheme or physical driver ABI. */
static uint32_t cpu_id(const struct d59b_context *context)
{
#ifdef SPIFFOS_D59B_HOSTED
    return context->host->cpu(context->host->opaque);
#else
    (void)context;
    uint32_t a,b,c,d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1u),"c"(0u):"memory");
    return b>>24;
#endif
}
/* Observe the executing stack, independently of recorded saved RSP.
 * Every helper executes on the same stack; no stack switch is allowed. */
static uintptr_t cpu_rsp(const struct d59b_context *context)
{
#ifdef SPIFFOS_D59B_HOSTED
    return context->host->rsp(context->host->opaque);
#else
    (void)context;
    uintptr_t rsp;
    __asm__ volatile("movq %%rsp,%0":"=r"(rsp)::"memory");
    return rsp;
#endif
}
/* Restore IF only after successful outermost release. A failure never
 * reaches this helper. STI is deliberately not a general restore API. */
static void cpu_enable(const struct d59b_context *context)
{
#ifdef SPIFFOS_D59B_HOSTED
    context->host->enable(context->host->opaque);
#else
    (void)context;
    __asm__ volatile("sti":::"memory");
#endif
}

/* Qwen predicate, independently checked against actual M10-A/D5.2b types.
 * Borrow metadata under IF=0; publish exact owner only after full validation.
 * Overflow/range checks precede pointer arithmetic; no memory dereference
 * through the observed RSP, allocation, state mutation or ownership transfer. */
static bool current_owner(const struct d59b_context *context, uintptr_t rsp, struct kernel_task_handle *out) {
    if (!context || !out) return false;
    if (!kernel_task_resources_check(context->resources, context->tasks)) return false;

    size_t slot = context->tasks->current_slot;
    if (slot >= KERNEL_TASK_CAPACITY) return false;

    const struct kernel_task_record *record = &context->tasks->tasks[slot];
    if (record->state != KERNEL_TASK_RUNNING || record->generation == 0) return false;

    const struct kernel_task_resource_record *res = &context->resources->task[slot];
    if (!res->installed || res->quarantined) return false;

    if (res->handle.slot != slot || res->handle.generation != record->generation) return false;

    const struct kernel_task_stack_binding *stack = &res->stack;
    if (stack->bytes == 0 || stack->bottom > UINTPTR_MAX-stack->bytes) return false;
    if (rsp < stack->bottom || rsp - stack->bottom >= stack->bytes) return false;

    *out = res->handle;
    return true;
}

/* Once-only boot binding; caller owns IF=0, valid borrowed state and CPU.
 * Reject reinitialization rather than clearing any latched D5.7 failure.
 * No task must be RUNNING yet; the entry operation separately checks RSP. */
bool d59b_context_init(struct d59b_context *context,
    struct d57_preempt_guard *guard,
    const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources) {
    if (!context || !guard || !tasks || !resources)
        return false;
    if (!backend_valid(context))
        return false;
    if (context->initialized)
        return false;
    if ((cpu_flags(context) & D59B_IF_MASK) != 0)
        return false;
    if (d59b_irq_depth != 0)
        return false;
    if (!guard->initialized || guard->faulted || guard->depth != 0)
        return false;
    if (!kernel_task_resources_check(resources, tasks))
        return false;
    context->expected_cpu = cpu_id(context);
    context->initialized = true;
    context->original_if = false;
    context->guard = guard;
    context->tasks = tasks;
    context->resources = resources;
    return true;
}

/* Sample ordered guest TSC after CLI/before restore to measure the held
 * exclusion span, including context checks and all nested transactions.
 * Host sanitizer tests do not claim hardware timing and use zero instead.
 * No conversion to microseconds, physical deadline or CPU quota is implied. */
static uint64_t cpu_cycles(void)
{
#ifdef SPIFFOS_D59B_HOSTED
    return 0;
#else
    uint32_t low,high;
    __asm__ volatile("lfence; rdtsc":"=a"(low),"=d"(high)::"memory");
    return ((uint64_t)high<<32)|low;
#endif
}
/* Faults are permanent; never reset ownership or enable IF on refusal.
 * A malformed caller object is a kernel programming error; all borrowed
 * pointers must designate valid adapter-owned objects for their lifetime. */
static bool reject_context(struct d59b_context *context)
{
    if(context!=NULL && context->guard!=NULL) context->guard->faulted=true;
    return false;
}
/* Authenticate execution only after actual CLI. The bounded resource
 * validator and live stack range bind CPU execution to sole M10-A owner.
 * No caller-provided IF/IRQ/task witness is accepted by kernel builds. */
static bool execution_owner(struct d59b_context *context,
                            struct kernel_task_handle *owner)
{
    return context->initialized && context->guard!=NULL &&
        context->guard->initialized && !context->guard->faulted &&
        context->guard->depth<=D57_MAX_GUARD_DEPTH &&
        (cpu_flags(context)&D59B_IF_MASK)==0 && d59b_irq_depth==0 &&
        cpu_id(context)==context->expected_cpu &&
        current_owner(context,cpu_rsp(context),owner);
}
/* Begin exact-generation exclusion, publishing only a successful D5.7
 * cookie. Preserve original IF at the outermost entry, never for nesting.
 * Null/invalid runtime admission retains IRQ exclusion and sticky fault. */
bool d59b_enter(struct d59b_context *context,uint32_t *cookie)
{
    if(!backend_valid(context)) {
#ifndef SPIFFOS_D59B_HOSTED
        /* Real CLI does not dereference context, even for a null API call. */
        (void)cpu_save_disable(context);
#endif
        return false;
    }
    const uint64_t entry=cpu_save_disable(context);
    const uint64_t started=cpu_cycles();
    struct kernel_task_handle owner={0};
    if(cookie==NULL || !execution_owner(context,&owner) ||
       (context->guard->depth!=0 && (entry&D59B_IF_MASK)!=0))
        return reject_context(context);
    if(!d57_guard_enter(context->guard,owner,false,true,cookie))
        return reject_context(context);
    if(context->guard->depth==1) {
        context->original_if=(entry&D59B_IF_MASK)!=0;
        context->entry_tsc=started;
    }
    return true;
}
/* Release only the exact RUNNING owner/top cookie under real IF=0.
 * Invalid depth is rejected before D5.7 can index its serial array.
 * Only the successful final release restores the saved original IF. */
bool d59b_exit(struct d59b_context *context,uint32_t cookie)
{
    if(!backend_valid(context)) {
#ifndef SPIFFOS_D59B_HOSTED
        /* Real CLI does not dereference context, even for a null API call. */
        (void)cpu_save_disable(context);
#endif
        return false;
    }
    const uint64_t entry=cpu_save_disable(context);
    struct kernel_task_handle owner={0};
    if((entry&D59B_IF_MASK)!=0 || !execution_owner(context,&owner) ||
       context->guard->depth==0)
        return reject_context(context);
    if(!d57_guard_exit(context->guard,owner,false,true,cookie))
        return reject_context(context);
    if(context->guard->depth==0) {
        const uint64_t finished=cpu_cycles();
        if(finished<context->entry_tsc) return reject_context(context);
        const uint64_t elapsed=finished-context->entry_tsc;
        if(elapsed>context->max_exclusion_tsc) context->max_exclusion_tsc=elapsed;
        if(context->completed_exclusions!=UINT64_MAX) ++context->completed_exclusions;
        if(context->original_if) cpu_enable(context);
    }
    return true;
}
