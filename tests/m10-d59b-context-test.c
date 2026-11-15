/* Purpose: independent hosted negatives for the D5.9-B CPU-context adapter.
 * Dependencies: real D5.7 guard, M10-A/D5.2b and M2; injected CPU only.
 * Invariants: never executes privileged instructions; no hardware claims.
 * References: ADR-m10-d59b-authentic-context.md and AGENTS.md. */
#include "m10-d59b-context.h"
#include "frame-allocator.h"
#include <assert.h>
#include <stdio.h>
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
/* Real-source allocator operations occur only following successful admission. */
static void positive(void)
{
    struct fixture f;
    setup(&f);
    struct memory_region region={4096u,5u*4096u,MEMORY_REGION_USABLE};
    struct frame_allocator allocator={0};
    uint8_t slots[8]={0};
    assert(frame_allocator_initialize(&allocator,&region,1u,slots,8u,1u)==FRAME_OK);
    for(unsigned i=0;i<20000u;++i) {
        bool original=(i&1u)!=0;
        f.cpu.enabled=original;
        unsigned enables=f.cpu.enables;
        uint32_t outer=0,inner=0;
        assert(d59b_enter(&f.context,&outer));
        assert(!f.cpu.enabled && f.guard.depth==1u);
        assert(d59b_enter(&f.context,&inner));
        assert(inner!=outer && f.guard.depth==2u);
        assert(!d57_guard_may_switch(&f.guard,true,true,true));
        uint64_t address=UINT64_MAX;
        assert(frame_allocate(&allocator,FRAME_OWNER_KERNEL,false,&address)==FRAME_OK);
        assert(frame_allocator_check(&allocator));
        assert(frame_release(&allocator,FRAME_OWNER_KERNEL,address)==FRAME_OK);
        assert(frame_allocator_check(&allocator));
        assert(d59b_exit(&f.context,inner));
        assert(!f.cpu.enabled && f.guard.depth==1u);
        assert(d59b_exit(&f.context,outer));
        assert(f.cpu.enabled==original && f.guard.depth==0 && !f.guard.faulted);
        assert(f.cpu.enables==enables+(original?1u:0u));
        assert(allocator.ordinary_free==4u && allocator.reserve_free==1u);
    }
}
/* Entry refusal leaves all scheduler/resource/allocator work unperformed. */
static void rejected_entries(void)
{
    for(unsigned kind=0;kind<8u;++kind) {
        struct fixture f;
        setup(&f);
        uint32_t cookie=UINT32_C(0xabc);
        f.cpu.enabled=true;
        switch(kind) {
        case 0: ++f.cpu.id; break;
        case 1: d59b_irq_depth=1; break;
        case 2: f.cpu.stack=UINT64_C(0x104000); break;
        case 3: f.resources.task[f.current.slot].quarantined=true; break;
        case 4: f.guard.depth=D57_MAX_GUARD_DEPTH+1u; break;
        case 5: ++f.resources.task[f.current.slot].handle.generation; break;
        case 6: assert(kernel_task_terminate(&f.tasks,f.current)==KERNEL_TASK_OK); break;
        default: f.cpu.stack=UINT64_C(0xfffff); break;
        }
        struct kernel_task_table before=f.tasks;
        struct kernel_task_resources resources_before=f.resources;
        assert(!d59b_enter(&f.context,&cookie));
        assert(cookie==UINT32_C(0xabc));
        assert(!f.cpu.enabled && f.cpu.enables==0u && f.guard.faulted);
        assert(memcmp(&before,&f.tasks,sizeof before)==0);
        assert(memcmp(&resources_before,&f.resources,sizeof resources_before)==0);
        d59b_irq_depth=0;
        f.cpu.id=7u;f.cpu.stack=UINT64_C(0x102000);
        assert(!d59b_enter(&f.context,&cookie));
        assert(!f.cpu.enabled && !d57_guard_may_switch(&f.guard,true,true,true));
        assert(!d57_guard_init(&f.guard));
    }
}
/* Bad exit, stale running generation and LIFO errors retain exclusion. */
static void rejected_exits(void)
{
    for(unsigned kind=0;kind<8u;++kind) {
        struct fixture f;
        setup(&f);
        f.cpu.enabled=true;
        uint32_t outer=0,inner=0;
        assert(d59b_enter(&f.context,&outer));
        assert(d59b_enter(&f.context,&inner));
        uint32_t attempted=inner;
        switch(kind) {
        case 0: attempted=outer; break;
        case 1: attempted=0; break;
        case 2: ++f.cpu.id; break;
        case 3: d59b_irq_depth=1; break;
        case 4: f.guard.depth=D57_MAX_GUARD_DEPTH+1u; break;
        case 5: f.cpu.enabled=true; break;
        case 6:
            ++f.tasks.tasks[f.current.slot].generation;
            ++f.resources.task[f.current.slot].handle.generation;
            break;
        default: f.cpu.stack=UINT64_C(0x104000); break;
        }
        assert(!d59b_exit(&f.context,attempted));
        assert(!f.cpu.enabled && f.cpu.enables==0u && f.guard.faulted);
        d59b_irq_depth=0;
        assert(!d59b_exit(&f.context,inner));
        assert(!f.cpu.enabled && f.cpu.enables==0u);
    }
}
/* All checks use unchanged production policy sources and mock CPU samples. */
int main(void)
{
    positive();rejected_entries();rejected_exits();
    puts("SPIFFOS_M10_D59B_CONTEXT_HOSTED_OK");
    return 0;
}
