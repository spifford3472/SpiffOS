/* Purpose: M10-D5.2b hosted reference/owner/quarantine regression.
 * Dependencies: M10-A table, D5.2a reaper, D5.2b static registry.
 * Invariants: stale handles never release; live refs block retirement;
 * no retry after allocator failure; all successful reuse advances generation.
 * Concurrency: single hosted CPU models owner-serialized IF=0 paths.
 * Reference: ADR-m10-d5-resource-ownership.md and AGENTS.md. */
#include "../src/kernel/kernel-task-resources.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned released, attempted;
static size_t last_owner;
static bool refuse;

/* Simulated exact-owner release; a refusal never retires backing. */
static bool release_owner(size_t index)
{
    ++attempted;
    if(refuse) return false;
    ++released;
    last_owner=index;
    return true;
}

/* The metadata starts at CREATED, permitting a registered stack before
 * first dispatch; each test can then transition it to TERMINATED. */
static struct kernel_task_handle create_bound(
    struct kernel_task_table *table, struct kernel_task_resources *resources,
    size_t owner)
{
    struct kernel_task_handle handle;
    assert(kernel_task_create(table,false,&handle)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding binding={
        .bottom=(uintptr_t)0x100000u + owner*0x10000u,
        .bytes=16384u,
        .saved_rsp=(uintptr_t)0x103fe0u + owner*0x10000u,
        .owner_index=owner
    };
    assert(kernel_task_resources_bind(resources,table,handle,&binding)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_check(resources,table));
    return handle;
}

/* Terminate via the same state transitions as the actual dispatcher. */
static void terminate_task(struct kernel_task_table *table,
                           struct kernel_task_handle handle)
{
    assert(kernel_task_make_runnable(table,handle)==KERNEL_TASK_OK);
    struct kernel_task_handle current;
    assert(kernel_task_dispatch(table,&current)==KERNEL_TASK_OK);
    assert(current.slot==handle.slot && current.generation==handle.generation);
    assert(kernel_task_terminate(table,handle)==KERNEL_TASK_OK);
}

/* A refused resource operation must leave both metadata structures intact. */
static void unchanged(const struct kernel_task_resources *resources,
    const struct kernel_task_resources *before,
    const struct kernel_task_table *table)
{
    assert(memcmp(resources,before,sizeof *resources)==0);
    assert(kernel_task_resources_check(resources,table));
}

static void blocked_and_quarantine(void)
{
    struct kernel_task_table table={0};
    struct kernel_task_resources resources={0};
    assert(kernel_task_init(&table));
    assert(kernel_task_resources_init(&resources));
    struct kernel_task_handle h=create_bound(&table,&resources,7);
    struct kernel_task_resource_record *r=&resources.task[h.slot];
    struct kernel_task_stack_binding duplicate=r->stack;
    struct kernel_task_handle h2;
    assert(kernel_task_create(&table,false,&h2)==KERNEL_TASK_OK);
    /* Reject both overlapping backing and duplicate owner tokens. */
    assert(kernel_task_resources_bind(&resources,&table,h2,&duplicate)==KERNEL_RESOURCE_BAD_STATE);
    duplicate.bottom += 0x10000u;
    duplicate.saved_rsp += 0x10000u;
    assert(kernel_task_resources_bind(&resources,&table,h2,&duplicate)==KERNEL_RESOURCE_BAD_STATE);
    assert(kernel_task_resources_ref(&resources,&table,h,KERNEL_RESOURCE_LOCK,true)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_ref(&resources,&table,h,KERNEL_RESOURCE_WAITER,true)==KERNEL_RESOURCE_OK);
    terminate_task(&table,h);
    unsigned releases_before=attempted;
    assert(kernel_task_resources_retire(&resources,&table,h,(uintptr_t)0x400000u,
              release_owner)==KERNEL_RESOURCE_BLOCKED);
    assert(attempted==releases_before);
    /* A terminated task's outstanding references cannot be silently
     * removed by the ledger. This deliberately blocks automatic reuse. */
    struct kernel_task_resources before=resources;
    assert(kernel_task_resources_ref(&resources,&table,h,KERNEL_RESOURCE_LOCK,false)==KERNEL_RESOURCE_BAD_STATE);
    unchanged(&resources,&before,&table);
    /* Model trusted sync-owner completion before termination on another
     * identity, then verify backing release rejection and quarantine. */
    struct kernel_task_table t2={0}; struct kernel_task_resources r2={0};
    assert(kernel_task_init(&t2) && kernel_task_resources_init(&r2));
    struct kernel_task_handle h3=create_bound(&t2,&r2,10);
    assert(kernel_task_resources_ref(&r2,&t2,h3,KERNEL_RESOURCE_LOCK,true)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_ref(&r2,&t2,h3,KERNEL_RESOURCE_LOCK,false)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_ref(&r2,&t2,h3,KERNEL_RESOURCE_WAITER,true)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_ref(&r2,&t2,h3,KERNEL_RESOURCE_WAITER,false)==KERNEL_RESOURCE_OK);
    terminate_task(&t2,h3);
    /* Even a correct off-stack address does not authorize teardown while
     * some unrelated ordinary task is the current CPU execution owner. */
    struct kernel_task_handle other, running;
    assert(kernel_task_create(&t2,false,&other)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&t2,other)==KERNEL_TASK_OK);
    assert(kernel_task_dispatch(&t2,&running)==KERNEL_TASK_OK);
    unsigned prior=attempted;
    assert(kernel_task_resources_retire(&r2,&t2,h3,(uintptr_t)0x400000u,
        release_owner)==KERNEL_RESOURCE_BAD_STATE);
    assert(attempted==prior);
    assert(kernel_task_yield(&t2,other)==KERNEL_TASK_OK);
    assert(kernel_task_resources_retire(&r2,&t2,h3,
        r2.task[h3.slot].stack.bottom+64u,release_owner)==KERNEL_RESOURCE_BAD_STATE);
    assert(attempted==releases_before);
    refuse=true;
    assert(kernel_task_resources_retire(&r2,&t2,h3,(uintptr_t)0x400000u,
        release_owner)==KERNEL_RESOURCE_QUARANTINED);
    assert(attempted==releases_before+1);
    assert(!t2.tasks[h3.slot].reclaimed);
    refuse=false;
    before=r2;
    assert(kernel_task_resources_retire(&r2,&t2,h3,(uintptr_t)0x400000u,
        release_owner)==KERNEL_RESOURCE_QUARANTINED);
    assert(attempted==releases_before+1);
    unchanged(&r2,&before,&t2);
}

static void owner_cycles(void)
{
    struct kernel_task_table table={0};
    struct kernel_task_resources resources={0};
    assert(kernel_task_init(&table) && kernel_task_resources_init(&resources));
    unsigned before=released;
    struct kernel_task_handle previous={UINT32_MAX,0};
    for(unsigned iteration=0;iteration<20000;++iteration) {
        struct kernel_task_handle id=create_bound(&table,&resources,0);
        if(iteration) {
            assert(id.slot==previous.slot && id.generation!=previous.generation);
            assert(kernel_task_resources_saved_rsp(&resources,&table,previous,
                (uintptr_t)0x103fc0u)==KERNEL_RESOURCE_BAD_HANDLE);
        }
        struct kernel_task_resources snapshot=resources;
        assert(kernel_task_resources_ref(&resources,&table,id,KERNEL_RESOURCE_LOCK,false)==KERNEL_RESOURCE_BAD_STATE);
        unchanged(&resources,&snapshot,&table);
        assert(kernel_task_resources_ref(&resources,&table,id,KERNEL_RESOURCE_WAITER,true)==KERNEL_RESOURCE_OK);
        assert(kernel_task_resources_ref(&resources,&table,id,KERNEL_RESOURCE_WAITER,false)==KERNEL_RESOURCE_OK);
        terminate_task(&table,id);
        assert(kernel_task_resources_saved_rsp(&resources,&table,id,
            (uintptr_t)0x103fc0u)==KERNEL_RESOURCE_OK);
        assert(kernel_task_resources_retire(&resources,&table,id,
            (uintptr_t)0x400000u,release_owner)==KERNEL_RESOURCE_OK);
        assert(kernel_task_resources_check(&resources,&table));
        assert(kernel_task_get(&table,id)->reclaimed);
        previous=id;
    }
    assert(released-before==20000 && last_owner==0);
}

static void corrupt_and_overflow(void)
{
    struct kernel_task_table table={0};
    struct kernel_task_resources resources={0};
    assert(kernel_task_init(&table) && kernel_task_resources_init(&resources));
    struct kernel_task_handle h=create_bound(&table,&resources,3);
    struct kernel_task_resource_record *r=&resources.task[h.slot];
    r->owned_locks=UINT_MAX;
    struct kernel_task_resources snapshot=resources;
    assert(kernel_task_resources_ref(&resources,&table,h,KERNEL_RESOURCE_LOCK,true)==KERNEL_RESOURCE_OVERFLOW);
    unchanged(&resources,&snapshot,&table);
    r->owned_locks=0;
    assert(kernel_task_resources_ref(&resources,&table,h,(enum kernel_task_reference_kind)100,true)==KERNEL_RESOURCE_INVALID);
    snapshot=resources;unchanged(&resources,&snapshot,&table);
    r->stack.saved_rsp=0;
    assert(!kernel_task_resources_check(&resources,&table));
    snapshot=resources;
    assert(kernel_task_resources_retire(&resources,&table,h,
        (uintptr_t)0x400000u,release_owner)==KERNEL_RESOURCE_BAD_HANDLE);
    assert(memcmp(&resources,&snapshot,sizeof snapshot)==0);
}

int main(void)
{
    blocked_and_quarantine();
    owner_cycles();
    corrupt_and_overflow();
    puts("SPIFFOS_M10_D5_RESOURCE_OWNERSHIP_OK");
    return 0;
}
