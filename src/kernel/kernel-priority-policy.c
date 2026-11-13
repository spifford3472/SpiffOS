/* Purpose: M10-D1 deterministic priority policy, independent of M10-A.
 * Responsibilities: validate an immutable ready snapshot; age-based
 * starvation resistance; periodic reserved supervisor selection.
 * Dependencies: kernel-priority-policy.h; no libc, heap or hardware.
 * Invariants: no side effects on success or failure. Selection never
 * creates, blocks, terminates, switches or otherwise owns a task.
 * Reference: docs/decisions/ADR-m10-d-preemption-proposal.md.
 */
#include "kernel-priority-policy.h"

#include <stdint.h>

/* Avoid uint64 overflow when a task has waited almost 2^64 decisions.
 * Saturation preserves monotonicity, while wait-age remains the first
 * tie-breaker after effective score. Never wrap a fairness counter. */
static uint64_t aged_score(const struct kernel_priority_candidate *candidate)
{
    uint64_t bonus =
        (uint64_t)candidate->priority * KERNEL_PRIORITY_AGE_WEIGHT;
    if (candidate->waiting_dispatches > UINT64_MAX - bonus)
        return UINT64_MAX;
    return candidate->waiting_dispatches + bonus;
}

/* Validate the entire input before choosing anything. A corrupted ready
 * list is a caller programming error, not permission to run a random task.
 * This function is read-only and is not safe to call from interrupt context. */
enum kernel_priority_result kernel_priority_choose(
    const struct kernel_priority_candidate *candidates, size_t count,
    uint64_t dispatch_sequence, uint32_t *selected_slot)
{
    if (selected_slot == NULL || count > KERNEL_PRIORITY_TASK_CAPACITY ||
        (count != 0 && candidates == NULL))
        return KERNEL_PRIORITY_INVALID;
    if (count == 0) return KERNEL_PRIORITY_EMPTY;

    bool seen[KERNEL_PRIORITY_TASK_CAPACITY] = {false};
    size_t supervisor_index = SIZE_MAX;
    for (size_t i = 0; i < count; ++i) {
        const struct kernel_priority_candidate *candidate = &candidates[i];
        if (candidate->slot >= KERNEL_PRIORITY_TASK_CAPACITY ||
            seen[candidate->slot] ||
            candidate->priority > KERNEL_PRIORITY_TIME_SENSITIVE ||
            candidate->priority < KERNEL_PRIORITY_BACKGROUND ||
            candidate->supervisor !=
                (candidate->slot == KERNEL_PRIORITY_SUPERVISOR_SLOT))
            return KERNEL_PRIORITY_INVALID;
        seen[candidate->slot] = true;
        if (candidate->supervisor) supervisor_index = i;
    }

    /* Reserve one of every four dispatch opportunities if the supervisor
     * is runnable. It may also win other slots on age or priority merit.
     * The actual CPU-time duration is addressed in later D milestones. */
    if (supervisor_index != SIZE_MAX &&
        dispatch_sequence % KERNEL_PRIORITY_SUPERVISOR_INTERVAL ==
            KERNEL_PRIORITY_SUPERVISOR_INTERVAL - 1) {
        *selected_slot = candidates[supervisor_index].slot;
        return KERNEL_PRIORITY_OK;
    }

    size_t best = 0;
    for (size_t i = 1; i < count; ++i) {
        uint64_t candidate_score = aged_score(&candidates[i]);
        uint64_t best_score = aged_score(&candidates[best]);
        if (candidate_score > best_score ||
            (candidate_score == best_score &&
             (candidates[i].waiting_dispatches >
                  candidates[best].waiting_dispatches ||
              (candidates[i].waiting_dispatches ==
                  candidates[best].waiting_dispatches &&
               candidates[i].slot < candidates[best].slot))))
            best = i;
    }
    *selected_slot = candidates[best].slot;
    return KERNEL_PRIORITY_OK;
}
