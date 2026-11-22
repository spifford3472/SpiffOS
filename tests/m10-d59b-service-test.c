/* Purpose: independent hosted M6 transaction/lifetime/exclusion checks.
 * Dependencies: real service/heap, D5.7 guard and M10-A/D5.2b; fake CPU only.
 * Invariants: client payload stays temporary, all roots retain identity, bad
 * admission/corruption never repairs service metadata or restores IF.
 * References: ADR-m10-d59b-service-transactions.md and AGENTS.md. */
#include "m10-d59b-service.h"
#include "m10-d59b-test-cpu.h"
#include <stdalign.h>
#include <stdio.h>

/* Each fixture models a fresh CPU/service lifetime, not guard reset/recovery. */
struct service_fixture {
    struct fixture cpu;
    struct memory_service service;
    alignas(16) uint8_t arena[MEMORY_SERVICE_MIN_ARENA_BYTES];
};
/* Build a real one-page/64-record service after authoritative CPU setup. */
static void service_setup(struct service_fixture *f)
{
    memset(f,0,sizeof *f);
    setup(&f->cpu);
    assert(memory_service_initialize(&f->service,f->arena,sizeof f->arena)==MEMORY_SERVICE_OK);
}
/* Cross both owner IDs with both original IF states. Every root and the
 * authoritative task/resource state must survive all client/OOM churn. */
static void service_churn(void)
{
    struct service_fixture f;
    service_setup(&f);
    const struct kernel_task_table tasks=f.cpu.tasks;
    const struct kernel_task_resources resources=f.cpu.resources;
    const struct memory_service_session session=*f.service.session_root;
    struct memory_service_workspace roots[MEMORY_SERVICE_WORKSPACE_COUNT];
    for(size_t i=0;i<MEMORY_SERVICE_WORKSPACE_COUNT;++i)
        roots[i]=*f.service.workspace_roots[i];
    for(unsigned i=0;i<20000u;++i) {
        const bool initial_if=(i&1u)!=0;
        f.cpu.cpu.enabled=initial_if;
        assert(d59b_service_probe(&f.cpu.context,&f.service,(i>>1)&1u));
        assert(f.cpu.cpu.enabled==initial_if && !f.cpu.guard.faulted && f.cpu.guard.depth==0);
        assert(f.service.heap.live_allocations==10);
        assert(memcmp(&session,f.service.session_root,sizeof session)==0);
        for(size_t j=0;j<MEMORY_SERVICE_WORKSPACE_COUNT;++j)
            assert(memcmp(&roots[j],f.service.workspace_roots[j],sizeof roots[j])==0);
    }
    assert(memcmp(&tasks,&f.cpu.tasks,sizeof tasks)==0);
    assert(memcmp(&resources,&f.cpu.resources,sizeof resources)==0);
    assert(memory_service_check(&f.service)==MEMORY_SERVICE_OK);
}
/* Refuse unsafe service state or CPU admission before mutating any service
 * byte. A live corrupt object is quarantined by sticky exclusion, not repaired. */
static void service_negatives(void)
{
    for(unsigned kind=0;kind<8u;++kind) {
        struct service_fixture f;
        service_setup(&f);
        switch(kind) {
        case 0: --f.service.heap.free_bytes; break;
        case 1: f.service.workspace_roots[0]->workspace_id=99; break;
        case 2: f.service.heap.block_capacity=1; break;
        case 3: f.service.heap.arena_bytes=8192; break;
        case 4: memory_service_retire(&f.service); break;
        case 5: d59b_irq_depth=1; break;
        case 6: ++f.cpu.cpu.id; break;
        default: f.cpu.cpu.stack=UINT64_C(0x104000); break;
        }
        const struct memory_service before=f.service;
        uint8_t before_arena[MEMORY_SERVICE_MIN_ARENA_BYTES];
        memcpy(before_arena,f.arena,sizeof before_arena);
        const struct kernel_task_table tasks=f.cpu.tasks;
        const struct kernel_task_resources resources=f.cpu.resources;
        f.cpu.cpu.enabled=true;
        assert(!d59b_service_probe(&f.cpu.context,&f.service,0));
        assert(f.cpu.guard.faulted && !f.cpu.cpu.enabled && f.cpu.cpu.enables==0);
        assert(memcmp(&before,&f.service,sizeof before)==0);
        assert(memcmp(before_arena,f.arena,sizeof before_arena)==0);
        assert(memcmp(&tasks,&f.cpu.tasks,sizeof tasks)==0);
        assert(memcmp(&resources,&f.cpu.resources,sizeof resources)==0);
        d59b_irq_depth=0;
        assert(!d59b_service_probe(&f.cpu.context,&f.service,0));
        assert(!d57_guard_init(&f.cpu.guard));
        assert(!f.cpu.cpu.enabled && f.cpu.cpu.enables==0);
    }
}
/* Null service and bad owner index are separate API errors, never allocation. */
static void service_arguments(void)
{
    struct service_fixture f;
    service_setup(&f);
    const struct memory_service before=f.service;
    f.cpu.cpu.enabled=true;
    assert(!d59b_service_probe(&f.cpu.context,&f.service,2));
    assert(f.cpu.cpu.enabled && !f.cpu.guard.faulted && f.cpu.guard.depth==0);
    assert(memcmp(&before,&f.service,sizeof before)==0);
    assert(!d59b_service_probe(&f.cpu.context,NULL,0));
    assert(!f.cpu.cpu.enabled && f.cpu.guard.faulted);
}
/* Hosted admission is a mock; actual IRQ0 acceptance is a separate QEMU gate. */
int main(void)
{
    service_churn();service_negatives();service_arguments();
    puts("SPIFFOS_M10_D59B_SERVICE_HOSTED_OK");
    return 0;
}
