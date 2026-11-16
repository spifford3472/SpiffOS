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

#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
#include "memory-map.h"
#ifdef SPIFFOS_D59B_HOSTED
#include <stdlib.h>
#endif
/* Direct trusted bridge, never caller-supplied policy or function pointer. */
extern bool memory_boot_d59b_scope_policy(uint64_t *offset,uint64_t *frames);
/* Qwen predicate: borrow exact D5.2b bindings under IF=0 and reject actual RSP
 * inside ANY installed range, including quarantine. Read-only; no stack
 * dereference, ownership transfer, allocation or synthetic CPU witness. */
static bool memory_off_registered_stacks(const struct d59b_context *context, uintptr_t actual_rsp) {
    if (!context || !kernel_task_resources_check(context->resources, context->tasks))
        return false;

    for (size_t i = 0; i < KERNEL_TASK_CAPACITY; ++i) {
        const struct kernel_task_resource_record *record = &context->resources->task[i];
        if (!record->installed) continue;
        uintptr_t bottom = record->stack.bottom;
        size_t bytes = record->stack.bytes;
        if (bytes == 0 || bytes > UINTPTR_MAX-bottom) return false;
        if (actual_rsp >= bottom && actual_rsp - bottom < bytes) return false;
    }
    return true;
}
static struct d59b_context memory_cpu;
static struct d59b_context *memory_bound,*memory_retired;
static bool memory_ready,memory_faulted,memory_ever_bound;
static uint32_t memory_boot_cpu;
static uint64_t memory_hhdm;

/* Latch irreversible domain failure. No reset, recovery or IF restoration. */
static bool memory_reject(void)
{
    memory_faulted=true;
    if(memory_bound!=NULL && memory_bound->guard!=NULL)
        memory_bound->guard->faulted=true;
    return false;
}
#ifdef SPIFFOS_D59B_HOSTED
/* Test-only CPU substitution before bootstrap; no kernel injection seam. */
bool d59b_memory_host_backend(const struct d59b_host_backend *backend)
{
    if(memory_ready || memory_faulted || memory_cpu.host!=NULL) return false;
    memory_cpu.host=backend;
    return backend_valid(&memory_cpu);
}
#endif

/* Caller already owns actual IF=0. Require the original boot CPU, off IRQ,
 * and a healthy initialized domain before interpreting lifecycle metadata. */
static bool memory_cpu_valid(void)
{
    return memory_ready && !memory_faulted &&
        (cpu_flags(&memory_cpu)&D59B_IF_MASK)==0 && d59b_irq_depth==0 &&
        cpu_id(&memory_cpu)==memory_boot_cpu;
}

/* Initialize the allocator-domain lifecycle once before any shared allocator
 * use. No synthetic task identity, guard reset, heap or register assumptions. */
bool d59b_memory_bootstrap(void)
{
    if(!backend_valid(&memory_cpu)) return false;
    const uint64_t entry=cpu_save_disable(&memory_cpu);
    if(memory_ready || memory_faulted || (entry&D59B_IF_MASK)!=0 ||
       d59b_irq_depth!=0) return memory_reject();
    memory_boot_cpu=cpu_id(&memory_cpu);
    memory_ready=true;
    return true;
}

/* Bind actual task authority once from the surviving bootstrap stack.
 * SUP has been selected in M10-A but no hardware task handoff is active yet.
 * Trusted retained policy is borrowed directly; no input HHDM witness is used. */
bool d59b_memory_bind(struct d59b_context *context)
{
    if(!backend_valid(&memory_cpu)) return false;
    const uint64_t entry=cpu_save_disable(&memory_cpu);
    if((entry&D59B_IF_MASK)!=0 || !memory_cpu_valid() || memory_bound!=NULL ||
       memory_retired!=NULL || memory_ever_bound || context==NULL ||
       !context->initialized || context->expected_cpu!=memory_boot_cpu ||
       context->guard==NULL || !context->guard->initialized ||
       context->guard->faulted || context->guard->depth!=0 ||
       !memory_off_registered_stacks(context,cpu_rsp(&memory_cpu)) ||
       context->tasks->current_slot!=KERNEL_TASK_SUPERVISOR_SLOT)
        return memory_reject();
    const struct kernel_task_record *supervisor=
        &context->tasks->tasks[KERNEL_TASK_SUPERVISOR_SLOT];
    const struct kernel_task_resource_record *binding=
        &context->resources->task[KERNEL_TASK_SUPERVISOR_SLOT];
    if(!binding->installed || binding->quarantined ||
       binding->handle.generation!=supervisor->generation)
        return memory_reject();
    uint64_t offset=0,frames=0;
    if(!supervisor->supervisor || supervisor->state!=KERNEL_TASK_RUNNING ||
       !memory_boot_d59b_scope_policy(&offset,&frames) || frames==0 ||
       frames>512u*1024u*1024u/MEMORY_PAGE_BYTES)
        return memory_reject();
    memory_hhdm=offset;
    memory_bound=context;
    memory_ever_bound=true;
    return true;
}

/* Require complete task quiescence on the surviving stack before retirement.
 * Caller must disarm IRQ selection first; selector independently requires bind.
 * Retain the exact ledger pointer until all installed bindings are retired. */
bool d59b_memory_unbind(struct d59b_context *context)
{
    if(!backend_valid(&memory_cpu)) return false;
    const uint64_t entry=cpu_save_disable(&memory_cpu);
    if((entry&D59B_IF_MASK)!=0 || !memory_cpu_valid() ||
       context==NULL || memory_bound!=context || context->guard==NULL ||
       context->guard->faulted || context->guard->depth!=0 ||
       context->tasks->current_slot!=SIZE_MAX ||
       !memory_off_registered_stacks(context,cpu_rsp(&memory_cpu)))
        return memory_reject();
    memory_retired=context;
    memory_bound=NULL;
    return true;
}

/* Forget borrowed ledger only after successful exact-owner retirement of all
 * rows; never erase a quarantined binding to manufacture reuse permission. */
bool d59b_memory_retirement_complete(struct d59b_context *context)
{
    if(!backend_valid(&memory_cpu)) return false;
    const uint64_t entry=cpu_save_disable(&memory_cpu);
    if((entry&D59B_IF_MASK)!=0 || !memory_cpu_valid() ||
       context==NULL || memory_bound!=NULL || memory_retired!=context ||
       context->tasks->current_slot!=SIZE_MAX || context->tasks->ready_count!=0 ||
       !memory_off_registered_stacks(context,cpu_rsp(&memory_cpu)))
        return memory_reject();
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i)
        if(context->resources->task[i].installed) return memory_reject();
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i) {
        const struct kernel_task_record *task=&context->tasks->tasks[i];
        if(task->state!=KERNEL_TASK_FREE &&
           !(task->state==KERNEL_TASK_TERMINATED && task->reclaimed))
            return memory_reject();
    }
    memory_retired=NULL;
    return true;
}

/* IRQ selector observes only binding identity under already-disabled IF.
 * No allocation, callback, guard mutation or alternate scheduling authority. */
bool d59b_memory_is_bound(const struct d59b_context *context)
{
    return memory_ready && !memory_faulted && context!=NULL &&
        memory_bound==context;
}

/* Query only inside a valid admitted scope. It grants no task identity or
 * ability to rebind after the diagnostic lifecycle has completed. */
bool d59b_memory_boot_only(void)
{
    return memory_cpu_valid() && memory_bound==NULL;
}

/* Resolve a physical frame against EXISTING immutable stack ranges only.
 * Overflow/corrupt authority conservatively denies all frame reuse/release.
 * Requires scope ownership; no stack contents are read, no new registry. */
static bool memory_bound_record(uint64_t physical,
    const struct kernel_task_resource_record **record)
{
    const struct d59b_context *context=
        memory_bound!=NULL?memory_bound:memory_retired;
    *record=NULL;
    if(!memory_cpu_valid()) { (void)memory_reject();return true; }
    if(context==NULL) return false;
    if(!kernel_task_resources_check(context->resources,context->tasks) ||
       physical>UINT64_MAX-memory_hhdm) {
        (void)memory_reject();return true;
    }
    const uint64_t mapped=physical+memory_hhdm;
    if(mapped>UINT64_MAX-MEMORY_PAGE_BYTES) {
        (void)memory_reject();return true;
    }
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i) {
        const struct kernel_task_resource_record *r=&context->resources->task[i];
        if(!r->installed) continue;
        const uint64_t bottom=r->stack.bottom;
        if(r->stack.bytes==0 || bottom>UINT64_MAX-r->stack.bytes) {
            (void)memory_reject();return true;
        }
        if(mapped<bottom+r->stack.bytes && bottom<mapped+MEMORY_PAGE_BYTES) {
            *record=r;return true;
        }
    }
    return false;
}

/* Do not recycle a FREE allocator slot still covered by retained binding. */
bool d59b_memory_frame_bound(uint64_t physical)
{
    const struct kernel_task_resource_record *record=NULL;
    return memory_bound_record(physical,&record);
}

/* Runtime cannot free ANY bound stack. Boot retirement requires exact
 * terminated generation, zero references and actual RSP outside ALL bindings.
 * D5.2b remains final release/quarantine authority; no bypass is installed. */
bool d59b_memory_frame_release_allowed(uint64_t physical)
{
    const struct kernel_task_resource_record *record=NULL;
    if(!memory_bound_record(physical,&record)) return true;
    if(memory_faulted || record==NULL || memory_bound!=NULL ||
       memory_retired==NULL || record->quarantined || record->owned_locks!=0 ||
       record->registered_waiters!=0 ||
       memory_retired->tasks->current_slot!=SIZE_MAX ||
       !memory_off_registered_stacks(memory_retired,cpu_rsp(&memory_cpu)))
        return false;
    const struct kernel_task_record *task=
        kernel_task_get(memory_retired->tasks,record->handle);
    return task!=NULL && task->state==KERNEL_TASK_TERMINATED &&
        !task->reclaimed;
}

/* Uncertain exit cannot return a recoverable error after mutation/publication.
 * Native abort is test-only; real kernel retains CLI/HLT without allocating,
 * invoking a reporter/backend or attempting a forced task/guard recovery. */
_Noreturn void d59b_memory_stop(void)
{
#ifdef SPIFFOS_D59B_HOSTED
    abort();
#else
    for(;;) __asm__ volatile("cli; hlt":::"memory");
#endif
}

/* Qwen entry glue: capture actual IF/CLI before inspecting shared domain
 * state. Runtime reuses D5.7 exact-generation cookies; bootstrap is a verified
 * single CPU with no independent task. Token is published ONLY on success.
 * Caller owns a zeroed token; refusal latches fault and retains IF=0. */
bool d59b_memory_scope_enter(struct d59b_memory_scope *scope) {
    if (!backend_valid(&memory_cpu)) return false;
    uint64_t entry = cpu_save_disable(&memory_cpu);
    if (!scope || scope->active || !memory_ready || memory_faulted || d59b_irq_depth != 0 ||
        cpu_id(&memory_cpu) != memory_boot_cpu)
        return memory_reject();
    uint32_t cookie = 0;
    if (memory_bound != NULL) {
        if (!memory_bound->guard || (memory_bound->guard->depth != 0 && (entry & D59B_IF_MASK)))
            return memory_reject();
        if (!d59b_enter(memory_bound, &cookie))
            return memory_reject();
        /* Record the actual IF captured before domain CLI in the EXISTING
         * trusted adapter, not in a second caller-owned restoration authority. */
        if(memory_bound->guard->depth==1u)
            memory_bound->original_if=(entry&D59B_IF_MASK)!=0;
    } else if (memory_retired != NULL) {
        if (memory_retired->tasks->current_slot != SIZE_MAX ||
            !memory_off_registered_stacks(memory_retired, cpu_rsp(&memory_cpu)))
            return memory_reject();
    }
    *scope = (struct d59b_memory_scope){memory_bound, entry, cookie, true};
    return true;
}

/* Qwen matching exit: reject unexpected IF/CPU/IRQ/identity/depth/cookie.
 * Runtime restoration belongs ONLY to the existing trusted adapter, never a
 * caller-owned token flag. Mark token inactive BEFORE a final exit may STI;
 * do not inspect shared context after another task can legitimately execute.
 * Errors retain IF=0/token for diagnosis; wrappers stop on uncertain exit. */
bool d59b_memory_scope_leave(struct d59b_memory_scope *scope) {
    if (!backend_valid(&memory_cpu)) return false;
    uint64_t entry = cpu_save_disable(&memory_cpu);
    if ((entry & D59B_IF_MASK) || !scope || !scope->active || !memory_cpu_valid())
        return memory_reject();
    if (scope->context != memory_bound)
        return memory_reject();
    if (scope->context != NULL) {
        if (!scope->context->guard)
            return memory_reject();
        bool tokenIF = (scope->previous_flags & D59B_IF_MASK) != 0;
        if (scope->context->guard->depth == 1) {
            if (tokenIF != scope->context->original_if)
                return memory_reject();
        } else if (tokenIF) {
            return memory_reject();
        }
        scope->active = false;
        if (!d59b_exit(scope->context, scope->cookie)) {
            scope->active = true;
            return memory_reject();
        }
        return true;
    } else {
        if (memory_retired != NULL &&
            (memory_retired->tasks->current_slot != SIZE_MAX ||
             !memory_off_registered_stacks(memory_retired, cpu_rsp(&memory_cpu))))
            return memory_reject();
    }
    bool restore = (scope->previous_flags & D59B_IF_MASK) != 0;
    scope->active = false;
    if (restore)
        cpu_enable(&memory_cpu);
    return true;
}
/* Check actual execution and lifecycle BEFORE a legacy boot-only API body.
 * Do not mask interrupts across a multi-page snapshot/report merely to add
 * a gate. Once unbound, selector cannot dispatch any independent task and
 * tagged IRQs cannot enter allocators; original boot borrowing contract stays.
 * No callback or state publication occurs in this bounded admission check. */
bool d59b_memory_boot_admit(void)
{
    struct d59b_memory_scope scope={0};
    if(!d59b_memory_scope_enter(&scope)) return false;
    const bool allowed=d59b_memory_boot_only();
    if(!d59b_memory_scope_leave(&scope)) d59b_memory_stop();
    return allowed;
}
#endif
