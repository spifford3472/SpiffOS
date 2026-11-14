/* Purpose: M10-D5.2c bounded cleanup-queue and reference-interlock tests.
 * Dependencies: authoritative M10 task table, D5.2b registry, D5.2c FIFO.
 * Invariants: no duplicate or stale queue entry, no release on active task
 * stack, blocked tasks retain refs and position, poisoned backing no retry.
 * Concurrency: hosted single CPU models dispatcher IF=0 calls.
 * References: ADR-m10-d5-supervisor-cleanup.md and AGENTS.md. */
#include "../src/kernel/kernel-task-cleanup.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned attempted,released;
static bool refuse;
static bool release_owner(size_t owner)
{
    (void)owner;
    ++attempted;
    if(refuse) return false;
    ++released;
    return true;
}

/* Create a bound task without changing the ordinary scheduler. Separate
 * backing and owner tokens prove queue registration is not slot-only. */
static struct kernel_task_handle new_task(
    struct kernel_task_table *table,struct kernel_task_resources *resources,
    size_t owner)
{
    struct kernel_task_handle h={0};
    assert(kernel_task_create(table,false,&h)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding stack={
        .bottom=(uintptr_t)0x100000u+owner*0x10000u,
        .bytes=16384,
        .saved_rsp=(uintptr_t)0x103fe0u+owner*0x10000u,
        .owner_index=owner
    };
    assert(kernel_task_resources_bind(resources,table,h,&stack)==KERNEL_RESOURCE_OK);
    return h;
}

/* Tests may terminate CREATED metadata before any dispatch, which is a
 * supported M10-A transition and still requires explicit frame release. */
static void terminate(struct kernel_task_table *table,
                      struct kernel_task_handle h)
{
    assert(kernel_task_terminate(table,h)==KERNEL_TASK_OK);
}

static void queue_fairness(void)
{
    struct kernel_task_table table={0};
    struct kernel_task_resources resources={0};
    struct kernel_task_cleanup_queue queue={0};
    assert(kernel_task_init(&table));
    assert(kernel_task_resources_init(&resources));
    assert(kernel_task_cleanup_init(&queue));
    struct kernel_task_handle blocked=new_task(&table,&resources,1);
    struct kernel_task_handle eligible=new_task(&table,&resources,2);
    assert(kernel_task_resources_ref(&resources,&table,blocked,
        KERNEL_RESOURCE_LOCK,true)==KERNEL_RESOURCE_OK);
    terminate(&table,blocked);
    terminate(&table,eligible);
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,blocked)==KERNEL_CLEANUP_OK);
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,blocked)==KERNEL_CLEANUP_INVALID);
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,eligible)==KERNEL_CLEANUP_OK);
    unsigned before=attempted;
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_OK);
    assert(attempted==before+1 && released==1 && queue.count==1);
    assert(queue.entries[0].slot==blocked.slot);
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_BLOCKED);
    assert(attempted==before+1);
    assert(!table.tasks[blocked.slot].reclaimed);
    /* Since a terminated task cannot release its own still-held lock,
     * queue service must not fabricate lock or waiter cleanup. */
    assert(kernel_task_resources_ref(&resources,&table,blocked,
        KERNEL_RESOURCE_LOCK,false)==KERNEL_RESOURCE_BAD_STATE);
    assert(queue.count==1);
    assert(kernel_task_cleanup_check(&queue));
    assert(kernel_task_resources_check(&resources,&table));
}

static void quarantine_and_reuse(void)
{
    struct kernel_task_table table={0};
    struct kernel_task_resources resources={0};
    struct kernel_task_cleanup_queue queue={0};
    assert(kernel_task_init(&table));
    assert(kernel_task_resources_init(&resources));
    assert(kernel_task_cleanup_init(&queue));
    struct kernel_task_handle bad=new_task(&table,&resources,0);
    terminate(&table,bad);
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,bad)==KERNEL_CLEANUP_OK);
    refuse=true;
    unsigned before=attempted;
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_QUARANTINED);
    assert(attempted==before+1 && queue.count==0 && queue.quarantines==1);
    assert(resources.task[bad.slot].quarantined);
    assert(!table.tasks[bad.slot].reclaimed);
    refuse=false;
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,bad)==KERNEL_CLEANUP_INVALID);
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_EMPTY);
    assert(attempted==before+1);
    struct kernel_task_handle good=new_task(&table,&resources,1);
    terminate(&table,good);
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,good)==KERNEL_CLEANUP_OK);
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        resources.task[good.slot].stack.bottom+64u,release_owner)==KERNEL_CLEANUP_INVALID);
    assert(queue.count==1 && attempted==before+1);
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_OK);
    assert(table.tasks[good.slot].reclaimed && queue.completed==1);
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,good)==KERNEL_CLEANUP_INVALID);
}

static void ordinary_current_rejected(void)
{
    struct kernel_task_table table={0};
    struct kernel_task_resources resources={0};
    struct kernel_task_cleanup_queue queue={0};
    assert(kernel_task_init(&table) && kernel_task_resources_init(&resources));
    assert(kernel_task_cleanup_init(&queue));
    struct kernel_task_handle h=new_task(&table,&resources,0);
    terminate(&table,h);
    assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,h)==KERNEL_CLEANUP_OK);
    struct kernel_task_handle running;
    assert(kernel_task_create(&table,false,&running)==KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(&table,running)==KERNEL_TASK_OK);
    assert(kernel_task_dispatch(&table,&running)==KERNEL_TASK_OK);
    unsigned before=attempted;
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_INVALID);
    assert(attempted==before && queue.count==1);
    assert(kernel_task_yield(&table,running)==KERNEL_TASK_OK);
    assert(kernel_task_cleanup_service(&queue,&resources,&table,
        (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_OK);
}

static void reuse_stress(void)
{
    struct kernel_task_table table={0};
    struct kernel_task_resources resources={0};
    struct kernel_task_cleanup_queue queue={0};
    assert(kernel_task_init(&table) && kernel_task_resources_init(&resources));
    assert(kernel_task_cleanup_init(&queue));
    struct kernel_task_handle prior={UINT32_MAX,0};
    unsigned before=released;
    for(unsigned i=0;i<20000;++i) {
        struct kernel_task_handle h=new_task(&table,&resources,0);
        if(i) {
            assert(h.slot==prior.slot && h.generation!=prior.generation);
            assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,prior)==KERNEL_CLEANUP_INVALID);
        }
        terminate(&table,h);
        assert(kernel_task_cleanup_enqueue(&queue,&resources,&table,h)==KERNEL_CLEANUP_OK);
        assert(kernel_task_cleanup_service(&queue,&resources,&table,
            (uintptr_t)0x400000u,release_owner)==KERNEL_CLEANUP_OK);
        assert(queue.count==0 && kernel_task_cleanup_check(&queue));
        assert(kernel_task_resources_check(&resources,&table));
        prior=h;
    }
    assert(released-before==20000);
}

int main(void)
{
    queue_fairness();
    quarantine_and_reuse();
    ordinary_current_rejected();
    reuse_stress();
    puts("SPIFFOS_M10_D5_SUPERVISOR_QUEUE_OK");
    return 0;
}
