/* Purpose: hosted compatibility of real D5.7 guard with genuine M2 allocation.
 * Dependencies: existing D5.7 guard source, real M2 allocator and fake IF.
 * Invariants: no production kernel wiring or privileged instructions.
 * References: ADR-m10-d57-preemption-and-priority.md and
 * ADR-m10-d59b-allocator-exclusion-contract.md.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "m10-d57-safety.h"
#include "frame-allocator.h"

/* Hold a verified D5.7 cookie while exercising the ordinary M2 frame path.
 * The owner token/IF are test-provided and not authenticated by hardware. */
static void verify_guarded_frame_transaction(void)
{
    struct d57_preempt_guard guard = {0};
    struct kernel_task_handle owner = {.slot = 3u, .generation = 17u};
    struct memory_region region = {4096u, 5u * 4096u, MEMORY_REGION_USABLE};
    struct frame_allocator allocator = {0};
    uint8_t slots[8] = {0};
    bool simulated_if = true;
    const bool saved_if = simulated_if;
    uint32_t cookie = 0;
    uint64_t address = 0;
    assert(d57_guard_init(&guard));
    assert(frame_allocator_initialize(&allocator, &region, 1u,
                                      slots, 8u, 1u) == FRAME_OK);
    simulated_if = false;
    assert(d57_guard_enter(&guard, owner, false, !simulated_if, &cookie));
    assert(cookie != 0u && guard.depth == 1u);
    assert(!d57_guard_may_switch(&guard, true, true, true));
    assert(frame_allocate(&allocator, FRAME_OWNER_KERNEL, false, &address) ==
           FRAME_OK);
    assert(frame_allocator_check(&allocator));
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, address) == FRAME_OK);
    assert(frame_allocator_check(&allocator));
    assert(d57_guard_exit(&guard, owner, false, !simulated_if, cookie));
    assert(!guard.faulted && guard.depth == 0u);
    simulated_if = saved_if;
    assert(simulated_if);
    assert(d57_guard_may_switch(&guard, true, true, true));
}

/* Invalid generation or LIFO cookie must permanently poison this diagnostic
 * guard; no recovery, forced unlock or stack reclamation is attempted. */
static void verify_sticky_fault(void)
{
    struct d57_preempt_guard guard = {0};
    struct kernel_task_handle owner = {.slot = 4u, .generation = 21u};
    struct kernel_task_handle stale = {.slot = 4u, .generation = 22u};
    uint32_t cookie = 0;
    assert(d57_guard_init(&guard));
    assert(d57_guard_enter(&guard, owner, false, true, &cookie));
    assert(!d57_guard_exit(&guard, stale, false, true, cookie));
    assert(guard.faulted);
    assert(!d57_guard_exit(&guard, owner, false, true, cookie));
    assert(!d57_guard_may_switch(&guard, true, true, true));
}

/* Positive and negative hosted checks do not imply real IRQ attestation. */
int main(void)
{
    verify_guarded_frame_transaction();
    verify_sticky_fault();
    puts("SPIFFOS_M10_D59B_D57_FRAME_COMPAT_OK");
    return 0;
}
