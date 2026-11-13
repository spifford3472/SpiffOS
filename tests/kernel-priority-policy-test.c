/* Purpose: M10-D1 starvation and supervisor quota regression tests.
 * Dependencies: kernel-priority-policy.h, C11 hosted assertion utilities.
 * Invariants: pure selection, all rejections atomic, sustained high-priority
 * hog load cannot starve continuously runnable Background tasks.
 * Reference: docs/decisions/ADR-m10-d-preemption-proposal.md.
 */
#include "../src/kernel/kernel-priority-policy.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Validate untrusted snapshots and the contract that errors preserve output. */
static void invalid_inputs(void)
{
    uint32_t chosen = 77;
    struct kernel_priority_candidate items[2] = {
        {.slot = 0, .priority = KERNEL_PRIORITY_NORMAL},
        {.slot = 1, .priority = KERNEL_PRIORITY_NORMAL}
    };
    assert(kernel_priority_choose(NULL, 0, 0, &chosen) == KERNEL_PRIORITY_EMPTY);
    assert(chosen == 77);
    assert(kernel_priority_choose(NULL, 1, 0, &chosen) == KERNEL_PRIORITY_INVALID);
    assert(kernel_priority_choose(items, 2, 0, NULL) == KERNEL_PRIORITY_INVALID);
    assert(chosen == 77);
    items[1].slot = 0;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_INVALID);
    items[1].slot = KERNEL_PRIORITY_SUPERVISOR_SLOT;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_INVALID);
    items[1].supervisor = true;
    items[1].priority = (enum kernel_priority_class)4;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_INVALID);
    assert(chosen == 77);
}

/* Score includes priority preference but wait age can overcome it.
 * Ties choose older candidate, then a stable lower slot number. */
static void aging_and_ties(void)
{
    struct kernel_priority_candidate items[] = {
        {.slot = 3, .priority = KERNEL_PRIORITY_TIME_SENSITIVE},
        {.slot = 2, .priority = KERNEL_PRIORITY_BACKGROUND,
         .waiting_dispatches = 25}
    };
    uint32_t chosen = 99;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_OK);
    assert(chosen == 2);
    items[1].waiting_dispatches = 24;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_OK);
    assert(chosen == 2); /* Equal score, older waiting task wins. */
    items[1].waiting_dispatches = 0;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_OK);
    assert(chosen == 3);
    items[0].priority = KERNEL_PRIORITY_NORMAL;
    items[1].priority = KERNEL_PRIORITY_NORMAL;
    items[0].waiting_dispatches = 0;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_OK);
    assert(chosen == 2);
    items[0].waiting_dispatches = UINT64_MAX;
    assert(kernel_priority_choose(items, 2, 0, &chosen) == KERNEL_PRIORITY_OK);
    assert(chosen == 3); /* Saturating arithmetic must not wrap. */
}

/* Simulate a perpetually runnable supervisor and fifteen adversarial tasks.
 * Even 14 time-sensitive hogs must leave CPU selections to Background;
 * every fourth dispatch is the supervisor while it remains runnable.
 * This is scheduling-decision fairness, not a proof of runtime CPU quantum. */
static void sustained_load(void)
{
    struct kernel_priority_candidate ready[KERNEL_PRIORITY_TASK_CAPACITY];
    uint64_t served[KERNEL_PRIORITY_TASK_CAPACITY] = {0};
    uint64_t longest_wait[KERNEL_PRIORITY_TASK_CAPACITY] = {0};
    for (uint32_t slot = 0; slot < KERNEL_PRIORITY_TASK_CAPACITY; ++slot) {
        ready[slot] = (struct kernel_priority_candidate){
            .slot = slot,
            .priority = slot == 0 ? KERNEL_PRIORITY_BACKGROUND :
                KERNEL_PRIORITY_TIME_SENSITIVE,
            .supervisor = slot == KERNEL_PRIORITY_SUPERVISOR_SLOT
        };
    }
    for (uint64_t decision = 0; decision < 4096; ++decision) {
        uint32_t selected = UINT32_MAX;
        assert(kernel_priority_choose(ready, KERNEL_PRIORITY_TASK_CAPACITY,
                                      decision, &selected) == KERNEL_PRIORITY_OK);
        if (decision % KERNEL_PRIORITY_SUPERVISOR_INTERVAL ==
            KERNEL_PRIORITY_SUPERVISOR_INTERVAL - 1)
            assert(selected == KERNEL_PRIORITY_SUPERVISOR_SLOT);
        assert(selected < KERNEL_PRIORITY_TASK_CAPACITY);
        ++served[selected];
        for (size_t i = 0; i < KERNEL_PRIORITY_TASK_CAPACITY; ++i) {
            if (i == selected) ready[i].waiting_dispatches = 0;
            else if (ready[i].waiting_dispatches != UINT64_MAX)
                ++ready[i].waiting_dispatches;
            if (ready[i].waiting_dispatches > longest_wait[i])
                longest_wait[i] = ready[i].waiting_dispatches;
        }
    }
    for (size_t i = 0; i < KERNEL_PRIORITY_TASK_CAPACITY; ++i)
        assert(served[i] >= 1);
    assert(served[0] >= 90);
    /* Under sustained all-runnable load, no slot may wait 64 dispatches.
     * This is a measured bounded test case, not a universal proof. */
    for (size_t i = 0; i < KERNEL_PRIORITY_TASK_CAPACITY; ++i)
        assert(longest_wait[i] < 64);
    assert(served[KERNEL_PRIORITY_SUPERVISOR_SLOT] >= 1024);
}

int main(void)
{
    invalid_inputs();
    aging_and_ties();
    sustained_load();
    puts("SPIFFOS_M10_D1_PRIORITY_POLICY_OK");
    return 0;
}
