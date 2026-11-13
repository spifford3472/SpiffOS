/* Purpose: deterministic M10-D4 real task-state host regressions.
 * Dependencies: D4 state controller and D1 priority selector, C11 assertions.
 * Invariants: supervisor cannot block, callers are checked, no wait age while
 * blocked, invalid recovery cannot re-enable a quarantined saved frame.
 * Reference: docs/decisions/ADR-m10-d4-priority-supervisor-proof.md.
 */
#include "../src/kernel/m10-d4-task-control.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* Verify the mandatory state lifecycle and exact privilege checks.
 * This is a host model; live QEMU tests verify actual IF exclusion. */
static void state_transitions(void)
{
    struct d4_task_control table={0};
    assert(d4_task_init(&table) && d4_task_check(&table));
    assert(!d4_task_init(&table));
    assert(d4_task_block(&table,1,0)==D4_TASK_BAD_CALLER);
    assert(d4_task_block(&table,D4_TASK_SUPERVISOR,
                         D4_TASK_SUPERVISOR)==D4_TASK_BAD_ARGUMENT);
    assert(d4_task_block(&table,1,1)==D4_TASK_OK);
    uint64_t credit=table.worker[1].waiting_dispatches;
    for(unsigned i=0;i<48;++i) {
        uint32_t selected=UINT32_MAX;
        assert(d4_task_choose(&table,&selected)==D4_TASK_OK);
        assert(selected!=1);
        assert(d4_task_note_dispatch(&table,selected)==D4_TASK_OK);
    }
    assert(table.worker[1].waiting_dispatches==credit);
    assert(d4_task_wake(&table,1,1)==D4_TASK_BAD_CALLER);
    assert(d4_task_wake(&table,1,D4_TASK_SUPERVISOR)==D4_TASK_OK);
    assert(d4_task_wake(&table,1,D4_TASK_SUPERVISOR)==D4_TASK_BAD_STATE);
    assert(d4_task_quarantine(&table,1,D4_TASK_SUPERVISOR,1)==
           D4_TASK_BAD_STATE);
    assert(d4_task_quarantine(&table,1,1,0)==D4_TASK_BAD_CALLER);
    assert(d4_task_quarantine(&table,1,D4_TASK_SUPERVISOR,0)==D4_TASK_OK);
    assert(d4_task_recover(&table,1,D4_TASK_SUPERVISOR,false)==
           D4_TASK_CORRUPT);
    assert(table.worker[1].status==D4_TASK_QUARANTINED);
    assert(d4_task_recover(&table,1,1,true)==D4_TASK_BAD_CALLER);
    assert(d4_task_recover(&table,1,D4_TASK_SUPERVISOR,true)==D4_TASK_OK);
    assert(table.worker[1].status==D4_TASK_READY);
    assert(d4_task_check(&table));
}

/* Run 20k decisions with all four priorities and a supervisor quota.
 * The low priority worker must make progress even under CPU hogs; each
 * worker is a separate candidate with strict nonwrapping wait credit. */
static void fairness(void)
{
    struct d4_task_control table={0};
    assert(d4_task_init(&table));
    uint64_t longest[D4_TASK_WORKERS]={0};
    for(unsigned turn=0;turn<20000;++turn) {
        uint32_t chosen=UINT32_MAX;
        assert(d4_task_choose(&table,&chosen)==D4_TASK_OK);
        if(turn%KERNEL_PRIORITY_SUPERVISOR_INTERVAL==
           KERNEL_PRIORITY_SUPERVISOR_INTERVAL-1)
            assert(chosen==D4_TASK_SUPERVISOR);
        assert(d4_task_note_dispatch(&table,chosen)==D4_TASK_OK);
        for(unsigned i=0;i<D4_TASK_WORKERS;++i)
            if(table.worker[i].waiting_dispatches>longest[i])
                longest[i]=table.worker[i].waiting_dispatches;
    }
    for(unsigned i=0;i<D4_TASK_WORKERS;++i) {
        assert(table.worker[i].dispatch_count>100);
        assert(longest[i]<128);
    }
    assert(table.supervisor_dispatches>=5000);
    assert(table.decision_sequence==20000);
}

int main(void)
{
    state_transitions();
    fairness();
    puts("SPIFFOS_M10_D4_TASK_CONTROL_OK");
    return 0;
}
