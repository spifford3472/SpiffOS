/* Purpose: hosted injection at each real frame allocator metadata store.
 * Dependencies: test-generated mutation-hook copy of genuine frame-allocate.c,
 *              M2 frame allocator, D5.9-B diagnostic guard and fake IF backend.
 * Invariants: never linked into the production kernel; no IRQ or hardware use.
 * References: ADR-m10-d59b-allocator-exclusion-contract.md.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "frame-allocator.h"
#include "gate.h"

struct fake_cpu { bool if_on; unsigned int restores; };
static struct fake_cpu cpu = {true, 0};
static struct gate_state guard;
static const struct gate_context owner = {true, false, {3, 17}};
static const struct gate_context irq = {true, true, {3, 17}};
static const struct gate_context stale = {true, false, {3, 18}};
static unsigned int hook_hits[4];

/* Return simulated IF; this is a hosted fixture only. */
static bool read_if(void *opaque) { return ((struct fake_cpu *)opaque)->if_on; }
/* Simulate disabling interrupts without privileged instructions. */
static void disable_if(void *opaque) { ((struct fake_cpu *)opaque)->if_on = false; }
/* Record exact saved-IF restoration at outermost exit. */
static void restore_if(void *opaque, bool enabled)
{
    struct fake_cpu *state = opaque;
    ++state->restores;
    state->if_on = enabled;
}
static struct gate_backend backend = {read_if, disable_if, restore_if, &cpu};

/* Read-only checkpoint within each temporary inconsistency interval.
 * Deliberately never inspects allocator counters or calls an allocator from
 * simulated IRQ context: the off-IRQ owner alone may mutate the records. */
void d59b_frame_test_hook(const struct frame_allocator *allocator,
                          unsigned int point)
{
    assert(allocator != NULL && point >= 1u && point <= 3u);
    assert(guard.held && guard.depth == 1u && !cpu.if_on);
    assert(guard.owner.slot == owner.owner.slot &&
           guard.owner.generation == owner.owner.generation);
    /* Attempted IRQ/stale admission cannot enter the exclusion region. */
    assert(gate_enter(&guard, &irq, &backend) == GATE_IRQ_CONTEXT);
    assert(gate_enter(&guard, &stale, &backend) == GATE_CONFLICT);
    assert(guard.depth == 1u && !cpu.if_on);
    ++hook_hits[point];
}

/* Invoke the actual frame allocation transaction while holding one exact
 * owner gate; verify the transient instrumentation saw all three stores. */
int main(void)
{
    struct memory_region region = {4096u, 5u * 4096u, MEMORY_REGION_USABLE};
    uint8_t slots[8] = {0};
    struct frame_allocator allocator = {0};
    assert(frame_allocator_initialize(&allocator, &region, 1u,
                                      slots, 8u, 1u) == FRAME_OK);
    assert(gate_enter(&guard, &owner, &backend) == GATE_OK);
    uint64_t address = 0;
    assert(frame_allocate(&allocator, FRAME_OWNER_KERNEL, false, &address) ==
           FRAME_OK);
    assert(address == 8192u);
    assert(hook_hits[1] == 1u && hook_hits[2] == 1u && hook_hits[3] == 1u);
    assert(frame_allocator_check(&allocator));
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, address) == FRAME_OK);
    assert(frame_allocator_check(&allocator));
    assert(gate_exit(&guard, &owner, &backend) == GATE_OK);
    assert(cpu.if_on && cpu.restores == 1u && !guard.held);
    puts("SPIFFOS_M10_D59B_MUTATION_HOOKS_OK");
    return 0;
}
