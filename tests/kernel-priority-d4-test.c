/* Purpose: hosted M10-D4 fairness under all four classes and readiness churn.
 * Responsibilities: simulate alternating runnable/blocked states and verify
 * every continuously runnable worker eventually receives CPU opportunities,
 * while the supervisor obtains every reserved fourth dispatch.
 * Dependencies: D1 pure kernel-priority-policy, hosted C11 assert.
 * Invariants: no real kernel state or IRQs touched; output unchanged on
 * invalid/empty snapshot; this is not proof of live blocked/wakeup semantics.
 * References: ADR-m10-d4-priority-supervisor-proof.md and AGENTS.md.
 */
#include "../src/kernel/kernel-priority-policy.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { D4_SLOTS=16, D4_TEST_DECISIONS=20000 };

/* Model one continuously ready supervisor and all four worker bands.
 * Selected workers reset age; blocked workers cannot age while blocked.
 * Reawakening a task starts at zero age and must not corrupt other tasks.
 * Every 4th dispatch is reserved for the runnable supervisor. */
static void four_bands_with_churn(void)
{
    struct kernel_priority_candidate all[D4_SLOTS];
    uint64_t service[D4_SLOTS]={0};
    uint64_t longest_wait[D4_SLOTS]={0};
    bool runnable[D4_SLOTS];
    for(uint32_t i=0;i<D4_SLOTS;++i) {
        all[i]=(struct kernel_priority_candidate){
            .slot=i,
            .priority=(enum kernel_priority_class)(i%4u),
            .waiting_dispatches=0,
            .supervisor=(i==KERNEL_PRIORITY_SUPERVISOR_SLOT)
        };
        runnable[i]=true;
    }
    for(uint64_t turn=0;turn<D4_TEST_DECISIONS;++turn) {
        /* Deterministic churn: worker slots 2 and 3 alternate between
         * blocked and ready every 37 dispatches. Never block supervisor. */
        runnable[2]=(turn/37u)%2u==0;
        runnable[3]=(turn/53u)%2u==0;
        struct kernel_priority_candidate ready[D4_SLOTS];
        size_t n=0;
        for(size_t i=0;i<D4_SLOTS;++i) {
            if(runnable[i]) ready[n++]=all[i];
            else all[i].waiting_dispatches=0;
        }
        uint32_t selected=UINT32_MAX;
        assert(kernel_priority_choose(ready,n,turn,&selected)==
               KERNEL_PRIORITY_OK);
        assert(selected<D4_SLOTS && runnable[selected]);
        if(turn%KERNEL_PRIORITY_SUPERVISOR_INTERVAL==
               KERNEL_PRIORITY_SUPERVISOR_INTERVAL-1)
            assert(selected==KERNEL_PRIORITY_SUPERVISOR_SLOT);
        ++service[selected];
        for(size_t i=0;i<D4_SLOTS;++i) {
            if(!runnable[i] || i==selected) all[i].waiting_dispatches=0;
            else ++all[i].waiting_dispatches;
            if(all[i].waiting_dispatches>longest_wait[i])
                longest_wait[i]=all[i].waiting_dispatches;
        }
    }
    for(size_t i=0;i<D4_SLOTS;++i) {
        assert(service[i]>0);
        assert(longest_wait[i]<128);
    }
    assert(service[KERNEL_PRIORITY_SUPERVISOR_SLOT]>=5000);
    /* Time-sensitive workers receive preference without permanent
     * starvation of Background workers under saturated CPU demand. */
    assert(service[4]>50);
    assert(service[7]>50);
}

/* A malformed or empty ready snapshot never fabricates an executable
 * task and never writes its caller's selected slot. */
static void rejected_dispatch(void)
{
    uint32_t selected=12345;
    assert(kernel_priority_choose(NULL,0,0,&selected)==KERNEL_PRIORITY_EMPTY);
    assert(selected==12345);
    struct kernel_priority_candidate bad[2]={
        {.slot=1,.priority=KERNEL_PRIORITY_NORMAL},
        {.slot=1,.priority=KERNEL_PRIORITY_INTERACTIVE}
    };
    assert(kernel_priority_choose(bad,2,3,&selected)==KERNEL_PRIORITY_INVALID);
    assert(selected==12345);
}

int main(void)
{
    four_bands_with_churn();
    rejected_dispatch();
    puts("SPIFFOS_M10_D4_HOST_PRIORITY_CHURN_OK");
    return 0;
}
