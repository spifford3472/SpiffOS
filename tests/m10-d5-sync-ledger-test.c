/* Purpose: D5.2c registered synchronization lifecycle acceptance.
 * Dependencies: M10 task table, resource ledger and one-owner/waiter proof.
 * Invariants: full-generation owner/wait references, no terminal owner
 * transfer, no silent reference drop, success permits eventual cleanup.
 * Concurrency: hosted single CPU models caller-owned IF=0 exclusion.
 * References: ADR-m10-d5-supervisor-cleanup.md and AGENTS.md. */
#include "../src/kernel/kernel-task-sync-proof.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Simulated physical owner completion; the ledger, not raw task metadata,
 * controls generation reuse in this test. */
static bool release_owner(size_t owner)
{
    (void)owner;
    return true;
}

static struct kernel_task_handle create(
    struct kernel_task_table *t,struct kernel_task_resources *r,size_t owner)
{
    struct kernel_task_handle h;
    assert(kernel_task_create(t,false,&h)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding b={
        .bottom=(uintptr_t)0x100000u+owner*0x10000u,
        .bytes=16384,
        .saved_rsp=(uintptr_t)0x103fe0u+owner*0x10000u,
        .owner_index=owner
    };
    assert(kernel_task_resources_bind(r,t,h,&b)==KERNEL_RESOURCE_OK);
    return h;
}
static void setup(struct kernel_task_table *t,
                  struct kernel_task_resources *r,
                  struct kernel_sync_proof_lock *lock)
{
    assert(kernel_task_init(t));
    assert(kernel_task_resources_init(r));
    assert(kernel_sync_proof_init(lock));
}
static void transfer(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct kernel_sync_proof_lock l={0};
    setup(&t,&r,&l);
    struct kernel_task_handle owner=create(&t,&r,0);
    struct kernel_task_handle waiter=create(&t,&r,1);
    struct kernel_task_handle extra=create(&t,&r,2);
    assert(kernel_sync_proof_acquire(&l,&r,&t,owner)==KERNEL_SYNC_OK);
    assert(r.task[owner.slot].owned_locks==1);
    assert(kernel_sync_proof_acquire(&l,&r,&t,owner)==KERNEL_SYNC_REJECTED);
    assert(kernel_sync_proof_acquire(&l,&r,&t,waiter)==KERNEL_SYNC_WAITING);
    assert(r.task[waiter.slot].registered_waiters==1);
    assert(kernel_sync_proof_acquire(&l,&r,&t,extra)==KERNEL_SYNC_REJECTED);
    assert(kernel_sync_proof_release(&l,&r,&t,extra)==KERNEL_SYNC_REJECTED);
    assert(kernel_sync_proof_release(&l,&r,&t,owner)==KERNEL_SYNC_OK);
    assert(l.owned && !l.waiting && l.owner.slot==waiter.slot);
    assert(r.task[owner.slot].owned_locks==0);
    assert(r.task[waiter.slot].owned_locks==1);
    assert(r.task[waiter.slot].registered_waiters==0);
    assert(kernel_sync_proof_release(&l,&r,&t,waiter)==KERNEL_SYNC_OK);
    assert(!l.owned && !l.waiting);
    assert(r.task[waiter.slot].owned_locks==0);
    assert(kernel_task_terminate(&t,owner)==KERNEL_TASK_OK);
    assert(kernel_task_terminate(&t,waiter)==KERNEL_TASK_OK);
    assert(kernel_task_terminate(&t,extra)==KERNEL_TASK_OK);
    assert(kernel_task_resources_check(&r,&t));
}
static void terminal_owner_denied(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct kernel_sync_proof_lock l={0};
    setup(&t,&r,&l);
    struct kernel_task_handle owner=create(&t,&r,0);
    struct kernel_task_handle waiter=create(&t,&r,1);
    assert(kernel_sync_proof_acquire(&l,&r,&t,owner)==KERNEL_SYNC_OK);
    assert(kernel_sync_proof_acquire(&l,&r,&t,waiter)==KERNEL_SYNC_WAITING);
    assert(kernel_task_terminate(&t,owner)==KERNEL_TASK_OK);
    struct kernel_task_resources snapshot=r;
    struct kernel_sync_proof_lock lock_snapshot=l;
    assert(kernel_sync_proof_release(&l,&r,&t,owner)==KERNEL_SYNC_REJECTED);
    assert(kernel_sync_proof_acquire(&l,&r,&t,waiter)==KERNEL_SYNC_REJECTED);
    assert(memcmp(&r,&snapshot,sizeof r)==0);
    assert(memcmp(&l,&lock_snapshot,sizeof l)==0);
    assert(r.task[owner.slot].owned_locks==1);
    assert(r.task[waiter.slot].registered_waiters==1);
}
static void repeated(void)
{
    struct kernel_task_table t={0};
    struct kernel_task_resources r={0};
    struct kernel_sync_proof_lock lock={0};
    setup(&t,&r,&lock);
    struct kernel_task_handle old={UINT32_MAX,0};
    for(unsigned i=0;i<20000;++i) {
        struct kernel_task_handle h=create(&t,&r,0);
        if(i) assert(h.generation!=old.generation);
        assert(kernel_sync_proof_acquire(&lock,&r,&t,h)==KERNEL_SYNC_OK);
        assert(kernel_sync_proof_release(&lock,&r,&t,h)==KERNEL_SYNC_OK);
        assert(kernel_task_terminate(&t,h)==KERNEL_TASK_OK);
        /* The trusted dispatcher releases exact registered backing rather
         * than bypassing the resource ledger's authoritative reaper gate. */
        assert(kernel_task_resources_retire(&r,&t,h,
            (uintptr_t)0x400000u,release_owner)==KERNEL_RESOURCE_OK);
        old=h;
    }
    assert(kernel_task_resources_check(&r,&t));
}

int main(void)
{
    transfer();
    terminal_owner_denied();
    repeated();
    puts("SPIFFOS_M10_D5_SYNC_LEDGER_OK");
    return 0;
}
