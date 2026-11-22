/* Purpose: D5.5 safety gate hosted sanitizer acceptance.
 * Responsibilities: prove task generation/stack/IRQ role admission, latched
 * supervisor deadline, nonmutation of authoritative resource/task tables.
 * Dependencies: D5.5 gate, D5.2b ledger and M10-A task lifecycle.
 * Invariants: never assume forged caller role is authenticated by policy;
 * no IRQ work, allocation or real context switches in hosted tests.
 * Reference: ADR-m10-d55-preemption-safety.md, AGENTS.md. */
#include "../src/kernel/kernel-preempt-safety-d55.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Register an ordinary known stack and dispatch the exact M10-A task. */
static struct kernel_task_handle create_worker(
    struct kernel_task_table *tasks,
    struct kernel_task_resources *resources)
{
    struct kernel_task_handle handle={0};
    assert(kernel_task_create(tasks,false,&handle)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding binding={
        .bottom=(uintptr_t)0x100000u+handle.slot*0x10000u,
        .bytes=16384,
        .saved_rsp=(uintptr_t)0x103fe0u+handle.slot*0x10000u,
        .owner_index=handle.slot
    };
    assert(kernel_task_resources_bind(resources,tasks,handle,
        &binding)==KERNEL_RESOURCE_OK);
    assert(kernel_task_make_runnable(tasks,handle)==KERNEL_TASK_OK);
    struct kernel_task_handle selected={0};
    assert(kernel_task_dispatch(tasks,&selected)==KERNEL_TASK_OK);
    assert(selected.slot==handle.slot &&
           selected.generation==handle.generation);
    return handle;
}

/* The policy reads a captured exact RSP and only validates it; the hosted
 * test does not pretend to authenticate a real CPU assembly entrypoint. */
static struct d55_cpu_evidence worker_evidence(
    struct kernel_task_handle worker)
{
    return (struct d55_cpu_evidence){
       .origin=D55_ORIGIN_COOPERATIVE,
       .irq_depth=0,.critical_depth=0,
       .interrupts_enabled=false,.on_dispatcher_stack=false,
       .actual_rsp=(uintptr_t)0x102000u+worker.slot*0x10000u,
       .current=worker
    };
}
static struct d55_cpu_evidence dispatcher_evidence(void)
{
    return (struct d55_cpu_evidence){
       .origin=D55_ORIGIN_DISPATCHER,
       .irq_depth=0,.critical_depth=0,
       .interrupts_enabled=false,.on_dispatcher_stack=true,
       .actual_rsp=(uintptr_t)0x4000000u,
       .current=(struct kernel_task_handle){0}
    };
}

/* Admit safe worker operations but reject IRQ-time stack cleanup, active
 * lock owner preemption, nested interrupt and forged task generation. */
static void test_admission(void)
{
    struct kernel_task_table tasks={0};
    struct kernel_task_resources resources={0};
    struct d55_service_monitor monitor={0};
    assert(kernel_task_init(&tasks));
    assert(kernel_task_resources_init(&resources));
    struct kernel_task_handle worker=create_worker(&tasks,&resources);
    assert(d55_monitor_init(&monitor,100,20));
    struct d55_cpu_evidence e=worker_evidence(worker);
    struct kernel_task_table saved_tasks=tasks;
    struct kernel_task_resources saved_resources=resources;
    assert(d55_admit(&monitor,&e,D55_OP_SYNC_EDIT,&tasks,&resources)==D55_ALLOWED);
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_ALLOWED);
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_DENIED);
    e.origin=D55_ORIGIN_IRQ_RETURN;
    e.irq_depth=1;
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_ALLOWED);
    assert(d55_admit(&monitor,&e,D55_OP_SYNC_EDIT,&tasks,&resources)==D55_DENIED);
    e.irq_depth=2;
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_DENIED);
    e.irq_depth=1;e.interrupts_enabled=true;
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_DENIED);
    e.interrupts_enabled=false;e.critical_depth=1;
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_DENIED);
    e.critical_depth=0;
    assert(kernel_task_resources_ref(&resources,&tasks,worker,
        KERNEL_RESOURCE_LOCK,true)==KERNEL_RESOURCE_OK);
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_DENIED);
    e.origin=D55_ORIGIN_COOPERATIVE;e.irq_depth=0;
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_ALLOWED);
    assert(kernel_task_resources_ref(&resources,&tasks,worker,
        KERNEL_RESOURCE_LOCK,false)==KERNEL_RESOURCE_OK);
    e.current.generation++;
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_DENIED);
    e.current=worker;e.actual_rsp=0x700000u;
    assert(d55_admit(&monitor,&e,D55_OP_SWITCH,&tasks,&resources)==D55_DENIED);
    e=worker_evidence(worker);
    assert(d55_admit(&monitor,&e,(enum d55_operation)99,&tasks,&resources)==D55_DENIED);
    assert(memcmp(&saved_tasks,&tasks,sizeof tasks)==0);
    assert(memcmp(&saved_resources,&resources,sizeof resources)==0);
}

/* Reaping is privileged non-IRQ only and cannot run from an active worker.
 * Dispatcher must truly be off every live stack, not merely claim role. */
static void test_privilege(void)
{
    struct kernel_task_table tasks={0};
    struct kernel_task_resources resources={0};
    struct d55_service_monitor monitor={0};
    assert(kernel_task_init(&tasks) && kernel_task_resources_init(&resources));
    struct kernel_task_handle worker=create_worker(&tasks,&resources);
    assert(d55_monitor_init(&monitor,300,10));
    struct d55_cpu_evidence e=dispatcher_evidence();
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_DENIED);
    assert(kernel_task_yield(&tasks,worker)==KERNEL_TASK_OK);
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_ALLOWED);
    assert(d55_admit(&monitor,&e,D55_OP_CANCEL,&tasks,&resources)==D55_ALLOWED);
    e.actual_rsp=resources.task[worker.slot].stack.bottom+32;
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_DENIED);
    e=dispatcher_evidence();e.irq_depth=1;
    assert(d55_admit(&monitor,&e,D55_OP_CANCEL,&tasks,&resources)==D55_DENIED);
    e=dispatcher_evidence();e.interrupts_enabled=true;
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_DENIED);
    e=dispatcher_evidence();e.critical_depth=1;
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_DENIED);
    e=dispatcher_evidence();e.on_dispatcher_stack=false;
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_DENIED);
}

/* Exactly-on-time service is safe. A missed interval, clock unavailable or
 * rollback latches a permanent denial; a dispatch attempt is not service. */
static void test_monitor(void)
{
    struct kernel_task_table tasks={0};
    struct kernel_task_resources resources={0};
    struct d55_service_monitor monitor={0};
    struct d55_cpu_evidence e=dispatcher_evidence();
    assert(kernel_task_init(&tasks) && kernel_task_resources_init(&resources));
    assert(!d55_monitor_init(&monitor,0,0));
    assert(d55_monitor_init(&monitor,100,20));
    assert(d55_monitor_sample(&monitor,true,120)==D55_ALLOWED);
    assert(monitor.last_service_ms==100 && monitor.verified_services==0);
    assert(d55_monitor_service(&monitor,&e,&tasks,&resources,true,120)==D55_ALLOWED);
    assert(monitor.last_service_ms==120 && monitor.verified_services==1);
    assert(d55_monitor_sample(&monitor,true,140)==D55_ALLOWED);
    assert(d55_monitor_sample(&monitor,true,141)==D55_MONITOR_FAULT);
    assert(d55_monitor_service(&monitor,&e,&tasks,&resources,true,141)==D55_MONITOR_FAULT);
    assert(d55_monitor_sample(&monitor,true,142)==D55_MONITOR_FAULT);
    assert(monitor.last_service_ms==120 && monitor.faulted);
    struct d55_service_monitor lost={0};
    assert(d55_monitor_init(&lost,200,20));
    assert(d55_monitor_sample(&lost,false,201)==D55_MONITOR_FAULT);
    struct d55_service_monitor rolled={0};
    assert(d55_monitor_init(&rolled,200,20));
    assert(d55_monitor_sample(&rolled,true,199)==D55_MONITOR_FAULT);
    struct d55_service_monitor late={0};
    assert(d55_monitor_init(&late,UINT64_MAX-10,10));
    assert(d55_monitor_sample(&late,true,UINT64_MAX)==D55_ALLOWED);
    assert(d55_monitor_sample(&late,true,0)==D55_MONITOR_FAULT);
}

/* Repetitive service samples, exact deadline edges and staged rollback
 * never mutate M10-A or D5.2b; preserve supervisor fail-closed semantics. */
static void stress_monitor(void)
{
    struct kernel_task_table tasks={0};
    struct kernel_task_resources resources={0};
    struct d55_service_monitor monitor={0};
    struct d55_cpu_evidence e=dispatcher_evidence();
    assert(kernel_task_init(&tasks) && kernel_task_resources_init(&resources));
    assert(d55_monitor_init(&monitor,0,5));
    for(uint64_t i=1;i<=20000;++i) {
        assert(d55_monitor_sample(&monitor,true,i)==D55_ALLOWED);
        assert(d55_monitor_service(&monitor,&e,&tasks,&resources,true,i)==D55_ALLOWED);
    }
    assert(monitor.verified_services==20000 && !monitor.faulted);
    assert(d55_monitor_sample(&monitor,true,20006)==D55_MONITOR_FAULT);
    assert(d55_admit(&monitor,&e,D55_OP_REAP,&tasks,&resources)==D55_MONITOR_FAULT);
}
int main(void)
{
    test_admission();
    test_privilege();
    test_monitor();
    stress_monitor();
    puts("SPIFFOS_M10_D55_PREEMPT_SAFETY_OK");
    return 0;
}
