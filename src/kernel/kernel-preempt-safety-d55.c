/* Purpose: D5.5 bounded, non-mutating context safety admission.
 * Responsibilities: validate actual task generation/stack evidence,
 * separate IRQ-safe switching from non-IRQ synchronization and reaping,
 * detect lost supervisor service with a latched monotonic deadline.
 * Dependencies: kernel-preempt-safety-d55.h, kernel-task.c, D5.2b ledger.
 * Invariants: does not switch CPU, change tasks, mutate locks or free stacks.
 * Concurrency: caller supplies verified CPU snapshot and holds exclusion.
 * References: ADR-m10-d55-preemption-safety.md, AGENTS.md. */
#include "kernel-preempt-safety-d55.h"
#include <stdint.h>

/* Compare slot AND generation, so recycled slot cannot inherit authority.
 * Pure helper, no ownership changes or IRQ assumptions. */
static bool same_handle(struct kernel_task_handle a,
                        struct kernel_task_handle b)
{
    return a.slot==b.slot && a.generation==b.generation;
}

/* RSP must be within a registered half-open stack interval. Stack ends
 * already validated for overflow by the D5.2b ledger. */
static bool within(uintptr_t address,
                   const struct kernel_task_stack_binding *stack)
{
    return address>=stack->bottom &&
        address<stack->bottom+stack->bytes;
}

/* Identify the active authoritative record. The supervisor is allowed
 * as the current task only in its immutable reserved slot. */
static bool active(const struct d55_cpu_evidence *e,
                   const struct kernel_task_table *table)
{
    if(e->current.slot>=KERNEL_TASK_CAPACITY ||
       table->current_slot!=e->current.slot) return false;
    const struct kernel_task_record *task=kernel_task_get(table,e->current);
    return task!=NULL && task->state==KERNEL_TASK_RUNNING;
}

/* Validate off-stack execution against ALL registered bindings. A claimed
 * dispatcher stack is not evidence unless actual RSP is separate too. */
static bool outside_all_stacks(uintptr_t rsp,
                     const struct kernel_task_resources *resources)
{
    if(rsp==0) return false;
    for(size_t i=0;i<KERNEL_TASK_CAPACITY;++i)
        if(resources->task[i].installed &&
           within(rsp,&resources->task[i].stack))
            return false;
    return true;
}

/* The caller must pair a verified snapshot with IF=0, no IRQ nesting,
 * and either a true independent boot context or running supervisor. */
static bool privileged_context(const struct d55_cpu_evidence *e,
    const struct kernel_task_table *table,
    const struct kernel_task_resources *resources)
{
    if(e->origin!=D55_ORIGIN_DISPATCHER ||
       e->interrupts_enabled || e->irq_depth!=0 ||
       e->critical_depth!=0 || !e->on_dispatcher_stack ||
       !outside_all_stacks(e->actual_rsp,resources)) return false;
    if(table->current_slot==SIZE_MAX) return true;
    return table->current_slot==KERNEL_TASK_SUPERVISOR_SLOT &&
           active(e,table) &&
           e->current.slot==KERNEL_TASK_SUPERVISOR_SLOT;
}

/* Establish a new one-way monitor only in zeroed caller-owned storage.
 * No reset/clear API is provided for a latched deadline fault. */
bool d55_monitor_init(struct d55_service_monitor *monitor,
                      uint64_t now_ms,uint64_t deadline_ms)
{
    if(monitor==NULL || monitor->initialized || deadline_ms==0)
        return false;
    monitor->initialized=true;
    monitor->faulted=false;
    monitor->deadline_ms=deadline_ms;
    monitor->last_sample_ms=now_ms;
    monitor->last_service_ms=now_ms;
    monitor->verified_services=0;
    return true;
}

/* Saturation is handled by subtraction AFTER now>=last; no unsigned
 * wrap can make a rolled-back clock appear to meet a service deadline.
 * Failure is persistent, never a request for unsafe automatic recovery. */
enum d55_result d55_monitor_sample(struct d55_service_monitor *monitor,
                                   bool clock_valid,uint64_t now_ms)
{
    if(monitor==NULL || !monitor->initialized ||
       monitor->deadline_ms==0) return D55_DENIED;
    if(monitor->faulted) return D55_MONITOR_FAULT;
    if(!clock_valid || now_ms<monitor->last_sample_ms ||
       now_ms<monitor->last_service_ms ||
       now_ms-monitor->last_service_ms>monitor->deadline_ms) {
        monitor->faulted=true;
        return D55_MONITOR_FAULT;
    }
    monitor->last_sample_ms=now_ms;
    return D55_ALLOWED;
}

/* Only actual privileged service completion updates the reference time.
 * A missed deadline cannot be rearmed by a late, forged service callback. */
enum d55_result d55_monitor_service(struct d55_service_monitor *monitor,
    const struct d55_cpu_evidence *e,
    const struct kernel_task_table *table,
    const struct kernel_task_resources *resources,
    bool clock_valid,uint64_t now_ms)
{
    if(monitor==NULL || !monitor->initialized || e==NULL ||
       !kernel_task_resources_check(resources,table) ||
       !privileged_context(e,table,resources)) return D55_DENIED;
    enum d55_result sampled=d55_monitor_sample(
        monitor,clock_valid,now_ms);
    if(sampled!=D55_ALLOWED) return sampled;
    monitor->last_service_ms=now_ms;
    if(monitor->verified_services!=UINT64_MAX)
        ++monitor->verified_services;
    return D55_ALLOWED;
}

/* No mutation: policy alone cannot establish that RFLAGS or observed RSP
 * were honestly captured. A later trusted low-level adapter must do so.
 * All invalid task states and unsafe entry paths fail closed. */
enum d55_result d55_admit(const struct d55_service_monitor *monitor,
    const struct d55_cpu_evidence *e,enum d55_operation operation,
    const struct kernel_task_table *table,
    const struct kernel_task_resources *resources)
{
    if(monitor==NULL || !monitor->initialized || e==NULL ||
       !kernel_task_resources_check(resources,table))
        return D55_DENIED;
    if(monitor->faulted || monitor->deadline_ms==0 ||
       monitor->last_sample_ms<monitor->last_service_ms ||
       monitor->last_sample_ms-monitor->last_service_ms>
           monitor->deadline_ms)
        return D55_MONITOR_FAULT;
    if(operation==D55_OP_CANCEL || operation==D55_OP_REAP)
        return privileged_context(e,table,resources) ?
            D55_ALLOWED:D55_DENIED;
    if(operation!=D55_OP_SWITCH && operation!=D55_OP_SYNC_EDIT)
        return D55_DENIED;
    if(e->on_dispatcher_stack || !active(e,table) ||
       e->critical_depth!=0 || e->current.slot>=KERNEL_TASK_SUPERVISOR_SLOT ||
       !resources->task[e->current.slot].installed ||
       resources->task[e->current.slot].quarantined ||
       !same_handle(resources->task[e->current.slot].handle,e->current) ||
       !within(e->actual_rsp,&resources->task[e->current.slot].stack))
        return D55_DENIED;
    if(operation==D55_OP_SYNC_EDIT)
        return (!e->interrupts_enabled && e->irq_depth==0 &&
            e->origin==D55_ORIGIN_COOPERATIVE) ?
            D55_ALLOWED:D55_DENIED;
    /* Cooperative owner may safely yield holding a lock after its IF=0
     * critical section has fully ended. IRQ-exit switching deliberately
     * denies any current owner/wait refs until proper spin/preempt-disable
     * integration has been validated. */
    if(e->origin==D55_ORIGIN_COOPERATIVE)
        return e->irq_depth==0 ? D55_ALLOWED:D55_DENIED;
    if(e->origin==D55_ORIGIN_IRQ_RETURN && e->irq_depth==1 &&
       !e->interrupts_enabled &&
       resources->task[e->current.slot].owned_locks==0 &&
       resources->task[e->current.slot].registered_waiters==0)
        return D55_ALLOWED;
    return D55_DENIED;
}
