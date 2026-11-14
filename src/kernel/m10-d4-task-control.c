/* Purpose: D4 fixed, auditable task readiness and supervisor control.
 * Dependencies: pure D1 priority policy only. Side effects: caller-owned
 * table; never writes hardware, allocates or touches execution stacks.
 * Invariants: supervisor always ready, caller authority checked, retained
 * age frozen during blocking, counters saturate. Caller owns IRQ masking.
 * Reference: docs/decisions/ADR-m10-d4-priority-supervisor-proof.md.
 */
#include "m10-d4-task-control.h"
#include <stdint.h>

/* Workers are assigned all four classes, not a synthetic ID alias. */
bool d4_task_init(struct d4_task_control *table)
{
    if(table==NULL || table->initialized) return false;
    for(size_t i=0;i<D4_TASK_WORKERS;++i) {
        table->worker[i]=(struct d4_task_entry){
            .status=D4_TASK_READY,
            .priority=(enum kernel_priority_class)i
        };
    }
    table->supervisor_dispatches=0;
    table->supervisor_wait=0;
    table->decision_sequence=0;
    table->initialized=true;
    return true;
}

/* Fail closed if a caller attempts to use uninitialized or corrupt
 * metadata; the supervisor does not have a mutable state field. */
bool d4_task_check(const struct d4_task_control *table)
{
    if(table==NULL || !table->initialized) return false;
    for(unsigned i=0;i<D4_TASK_WORKERS;++i)
        if(table->worker[i].status<D4_TASK_READY ||
           table->worker[i].status>D4_TASK_QUARANTINED ||
           (table->worker[i].priority<(enum kernel_priority_class)i ||
            table->worker[i].priority>KERNEL_PRIORITY_TIME_SENSITIVE))
            return false;
    return true;
}

/* A worker blocks its own execution eligibility, not the supervisor's.
 * The task's saved stack remains resident until it is woken or cleaned up. */
enum d4_task_result d4_task_block(struct d4_task_control *table,
                                 unsigned target, unsigned current_slot)
{
    if(!d4_task_check(table)) return D4_TASK_CORRUPT;
    if(target>=D4_TASK_WORKERS) return D4_TASK_BAD_ARGUMENT;
    if(target!=current_slot) return D4_TASK_BAD_CALLER;
    if(table->worker[target].status!=D4_TASK_READY)
        return D4_TASK_BAD_STATE;
    table->worker[target].status=D4_TASK_BLOCKED;
    return D4_TASK_OK;
}

/* Only the known supervisor may wake a blocked worker. Retaining
 * previous waiting credit prevents starvation during short wake windows. */
enum d4_task_result d4_task_wake(struct d4_task_control *table,
                                unsigned target, unsigned caller_slot)
{
    if(!d4_task_check(table)) return D4_TASK_CORRUPT;
    if(target>=D4_TASK_WORKERS) return D4_TASK_BAD_ARGUMENT;
    if(caller_slot!=D4_TASK_SUPERVISOR) return D4_TASK_BAD_CALLER;
    if(table->worker[target].status!=D4_TASK_BLOCKED)
        return D4_TASK_BAD_STATE;
    table->worker[target].status=D4_TASK_READY;
    return D4_TASK_OK;
}

/* Supervisor quarantines only an inactive worker. The running worker's
 * current frame cannot be invalidated underneath the interrupt epilogue. */
enum d4_task_result d4_task_quarantine(struct d4_task_control *table,
                                     unsigned target, unsigned caller_slot,
                                     unsigned current_slot)
{
    if(!d4_task_check(table)) return D4_TASK_CORRUPT;
    if(target>=D4_TASK_WORKERS) return D4_TASK_BAD_ARGUMENT;
    if(caller_slot!=D4_TASK_SUPERVISOR) return D4_TASK_BAD_CALLER;
    if(target==current_slot) return D4_TASK_BAD_STATE;
    if(table->worker[target].status!=D4_TASK_READY)
        return D4_TASK_BAD_STATE;
    table->worker[target].status=D4_TASK_QUARANTINED;
    return D4_TASK_OK;
}

/* Recovery never manufactures a frame. Supervisor must verify that a
 * saved 20-qword IRQ frame still lies on its separately owned stack. */
enum d4_task_result d4_task_recover(struct d4_task_control *table,
                                   unsigned target, unsigned caller_slot,
                                   bool frame_valid)
{
    if(!d4_task_check(table)) return D4_TASK_CORRUPT;
    if(target>=D4_TASK_WORKERS) return D4_TASK_BAD_ARGUMENT;
    if(caller_slot!=D4_TASK_SUPERVISOR) return D4_TASK_BAD_CALLER;
    if(table->worker[target].status!=D4_TASK_QUARANTINED)
        return D4_TASK_BAD_STATE;
    if(!frame_valid) return D4_TASK_CORRUPT;
    table->worker[target].status=D4_TASK_READY;
    return D4_TASK_OK;
}

/* The pure priority engine validates all candidates before choosing a
 * slot. No fake runnable record is created for BLOCKED or QUARANTINED. */
enum d4_task_result d4_task_choose(const struct d4_task_control *table,
                                  uint32_t *slot)
{
    if(!d4_task_check(table)) return D4_TASK_CORRUPT;
    if(slot==NULL) return D4_TASK_BAD_ARGUMENT;
    struct kernel_priority_candidate candidates[D4_TASK_CAPACITY];
    size_t n=0;
    candidates[n++]=(struct kernel_priority_candidate){
        .slot=D4_TASK_SUPERVISOR,.priority=KERNEL_PRIORITY_NORMAL,
        .waiting_dispatches=table->supervisor_wait,.supervisor=true
    };
    for(uint32_t i=0;i<D4_TASK_WORKERS;++i)
        if(table->worker[i].status==D4_TASK_READY)
            candidates[n++]=(struct kernel_priority_candidate){
                .slot=i,.priority=table->worker[i].priority,
                .waiting_dispatches=table->worker[i].waiting_dispatches
            };
    return kernel_priority_choose(candidates,n,table->decision_sequence,
        slot)==KERNEL_PRIORITY_OK ? D4_TASK_OK : D4_TASK_CORRUPT;
}

/* Account only actual READY tasks and the supervisor after the selected
 * frame has been validated. Every fourth supervisor reservation remains
 * external to this function and is checked by the D4 QEMU harness. */
enum d4_task_result d4_task_note_dispatch(struct d4_task_control *table,
                                          uint32_t selected_slot)
{
    if(!d4_task_check(table)) return D4_TASK_CORRUPT;
    if(selected_slot!=D4_TASK_SUPERVISOR &&
       (selected_slot>=D4_TASK_WORKERS ||
        table->worker[selected_slot].status!=D4_TASK_READY))
        return D4_TASK_BAD_STATE;
    if(table->decision_sequence!=UINT64_MAX) ++table->decision_sequence;
    if(selected_slot==D4_TASK_SUPERVISOR) {
        if(table->supervisor_dispatches!=UINT64_MAX)
            ++table->supervisor_dispatches;
        table->supervisor_wait=0;
    } else if(table->supervisor_wait!=UINT64_MAX) ++table->supervisor_wait;
    for(uint32_t i=0;i<D4_TASK_WORKERS;++i) {
        struct d4_task_entry *worker=&table->worker[i];
        if(worker->status!=D4_TASK_READY) continue;
        if(i==selected_slot) {
            worker->waiting_dispatches=0;
            if(worker->dispatch_count!=UINT64_MAX) ++worker->dispatch_count;
        } else if(worker->waiting_dispatches!=UINT64_MAX)
            ++worker->waiting_dispatches;
    }
    return D4_TASK_OK;
}

/* A diagnostic fixed one-waiter lock starts unowned; the IDs are exact
 * worker slots, never physical pointer addresses or user input. */
bool d4_task_lock_init(struct d4_task_lock *lock)
{
    if(lock==NULL || lock->initialized) return false;
    lock->owner=UINT32_MAX;
    lock->waiter=UINT32_MAX;
    lock->initialized=true;
    return true;
}

/* Under caller-held IF=0, a high-priority waiter blocks itself and raises
 * the READY owner's *effective* priority for the duration of this lock.
 * Donation is bounded to one waiter and cannot form dependency cycles. */
enum d4_task_result d4_task_lock_acquire(struct d4_task_control *table,
                                        struct d4_task_lock *lock,
                                        unsigned current_worker)
{
    if(!d4_task_check(table) || lock==NULL || !lock->initialized)
        return D4_TASK_CORRUPT;
    if(current_worker>=D4_TASK_WORKERS)
        return D4_TASK_BAD_CALLER;
    if(table->worker[current_worker].status!=D4_TASK_READY)
        return D4_TASK_BAD_STATE;
    if(lock->owner==UINT32_MAX) {
        lock->owner=current_worker;
        return D4_TASK_OK;
    }
    if(lock->owner>=D4_TASK_WORKERS || lock->waiter!=UINT32_MAX ||
       lock->owner==current_worker ||
       table->worker[lock->owner].status!=D4_TASK_READY)
        return D4_TASK_BAD_STATE;
    if(d4_task_block(table,current_worker,current_worker)!=D4_TASK_OK)
        return D4_TASK_CORRUPT;
    lock->waiter=current_worker;
    if(table->worker[lock->owner].priority<
       table->worker[current_worker].priority)
        table->worker[lock->owner].priority=
            table->worker[current_worker].priority;
    return D4_TASK_WAITING;
}

/* Release restores base priority and performs a precise lock-authorized
 * wake: only this one previously blocked waiter may become runnable.
 * The lock transfers ownership atomically under caller-held IF=0, so
 * the waiter resumes on its existing saved stack without a race. */
enum d4_task_result d4_task_lock_release(struct d4_task_control *table,
                                        struct d4_task_lock *lock,
                                        unsigned current_worker)
{
    if(!d4_task_check(table) || lock==NULL || !lock->initialized)
        return D4_TASK_CORRUPT;
    if(current_worker>=D4_TASK_WORKERS ||
       lock->owner!=current_worker)
        return D4_TASK_BAD_CALLER;
    uint32_t waiter=lock->waiter;
    if(waiter!=UINT32_MAX &&
       (waiter>=D4_TASK_WORKERS ||
        table->worker[waiter].status!=D4_TASK_BLOCKED))
        return D4_TASK_CORRUPT;
    table->worker[current_worker].priority=
        (enum kernel_priority_class)current_worker;
    if(waiter==UINT32_MAX) lock->owner=UINT32_MAX;
    else {
        table->worker[waiter].status=D4_TASK_READY;
        lock->owner=waiter;
        lock->waiter=UINT32_MAX;
    }
    return D4_TASK_OK;
}
