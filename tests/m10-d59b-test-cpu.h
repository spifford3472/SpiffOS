/* Purpose: shared native-only CPU fixture for D5.9-B regression tests.
 * Dependencies: real task/resource APIs with injected architectural samples.
 * Invariants: no privileged instructions or claims of hardware evidence.
 * Reference: ADR-m10-d59b-authentic-context.md and AGENTS.md. */
#ifndef SPIFFOS_D59B_TEST_CPU_H
#define SPIFFOS_D59B_TEST_CPU_H
#include "m10-d59b-context.h"
#include <assert.h>
#include <string.h>
/* Fake CPU state is confined to this hosted executable. */
struct fake_cpu { bool enabled; uint32_t id; uintptr_t stack; unsigned enables; };
struct fixture {
    struct fake_cpu cpu;
    struct d59b_host_backend backend;
    struct d59b_context context;
    struct d57_preempt_guard guard;
    struct kernel_task_table tasks;
    struct kernel_task_resources resources;
    struct kernel_task_handle current;
};
/* Simulate PUSHFQ/CLI, returning the original IF in architectural bit 9. */
static uint64_t save_disable(void *opaque)
{
    struct fake_cpu *cpu=opaque;
    uint64_t flags=cpu->enabled ? UINT64_C(0x202) : UINT64_C(2);
    cpu->enabled=false;
    return flags;
}
/* Borrow a fake architectural flags sample without mutation. */
static uint64_t flags(void *opaque)
{ return ((struct fake_cpu *)opaque)->enabled ? UINT64_C(0x202) : UINT64_C(2); }
/* Read the fake boot-CPU identity; never infers it from a task slot. */
static uint32_t cpu_id(void *opaque) { return ((struct fake_cpu *)opaque)->id; }
/* Read the fake live stack pointer, independent of saved task context. */
static uintptr_t rsp(void *opaque) { return ((struct fake_cpu *)opaque)->stack; }
/* Count every IF restoration; failed exits must never call this. */
static void enable(void *opaque)
{
    struct fake_cpu *cpu=opaque;
    cpu->enabled=true;
    ++cpu->enables;
}
/* Build an actual RUNNING M10-A task and generation-bound owned stack. */
static void setup(struct fixture *f)
{
    memset(f,0,sizeof *f);
    d59b_irq_depth=0;
    f->cpu=(struct fake_cpu){false,7u,UINT64_C(0x102000),0u};
    f->backend=(struct d59b_host_backend){save_disable,flags,cpu_id,rsp,enable,&f->cpu};
    f->context.host=&f->backend;
    assert(kernel_task_init(&f->tasks));
    assert(kernel_task_resources_init(&f->resources));
    assert(kernel_task_create(&f->tasks,false,&f->current)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding binding={UINT64_C(0x100000),16384u,
        UINT64_C(0x103fe0),0u};
    assert(kernel_task_resources_bind(&f->resources,&f->tasks,f->current,&binding)==KERNEL_RESOURCE_OK);
    assert(kernel_task_make_runnable(&f->tasks,f->current)==KERNEL_TASK_OK);
    struct kernel_task_handle selected={0};
    assert(kernel_task_dispatch(&f->tasks,&selected)==KERNEL_TASK_OK);
    assert(d57_guard_init(&f->guard));
    assert(d59b_context_init(&f->context,&f->guard,&f->tasks,&f->resources));
}

#endif
