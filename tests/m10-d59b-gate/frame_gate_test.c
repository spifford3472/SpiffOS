/* Purpose: test the hosted M10-D5.9-B gate around the *real* M2 frame allocator.
 * Dependencies: existing frame allocator and diagnostic-only gate, with mock IF.
 * Invariants: no hardware access, no IRQ execution, no production API changes.
 * References: ADR-m10-d59b-allocator-exclusion-contract.md and ADR-frame-allocator.md.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "frame-allocator.h"
#include "gate.h"

struct fake_cpu {
    bool interrupts_enabled;
    unsigned int disable_calls;
    unsigned int restore_calls;
};

/* Read the simulated interrupt flag; no hardware access or mutation. */
static bool fake_read_if(void *opaque)
{
    return ((struct fake_cpu *)opaque)->interrupts_enabled;
}

/* Simulate disabling IF. Only the fixture holds this caller-owned CPU object. */
static void fake_disable_if(void *opaque)
{
    struct fake_cpu *cpu = opaque;
    ++cpu->disable_calls;
    cpu->interrupts_enabled = false;
}

/* Restore the exact saved IF from the gate's final exit. */
static void fake_restore_if(void *opaque, bool enabled)
{
    struct fake_cpu *cpu = opaque;
    ++cpu->restore_calls;
    cpu->interrupts_enabled = enabled;
}

/* Only the exact registered non-IRQ gate owner may perform a frame operation.
 * This test wrapper is not linked into the kernel and cannot secure real callers.
 * It proves rejected admission never even calls the real allocator. */
static enum frame_result fixture_allocate(struct gate_state *gate,
    const struct gate_context *context, struct frame_allocator *allocator,
    enum frame_owner owner, bool reserved, uint64_t *address)
{
    if (!gate->held || gate->depth == 0 || !context->trusted || context->in_irq ||
        context->owner.generation == 0 ||
        gate->owner.slot != context->owner.slot ||
        gate->owner.generation != context->owner.generation) {
        return FRAME_FORBIDDEN;
    }
    return frame_allocate(allocator, owner, reserved, address);
}

/* Verify IF preservation, actual allocator accounting, OOM rollback and
 * rejected owner/IRQ callers without mutating the allocator. */
static void run_case(bool initial_if)
{
    struct fake_cpu cpu = {.interrupts_enabled = initial_if};
    struct gate_backend backend = {fake_read_if, fake_disable_if,
                                   fake_restore_if, &cpu};
    struct gate_state gate = {0};
    const struct gate_context owner = {true, false, {2, 7}};
    const struct gate_context stale = {true, false, {2, 8}};
    const struct gate_context irq = {true, true, {2, 7}};
    const struct gate_context untrusted = {false, false, {2, 7}};
    struct memory_region region = {4096, 5 * 4096, MEMORY_REGION_USABLE};
    uint8_t slots[8] = {0};
    struct frame_allocator allocator = {0};
    assert(frame_allocator_initialize(&allocator, &region, 1,
                                      slots, 8, 1) == FRAME_OK);
    assert(frame_allocator_check(&allocator));

    uint64_t output = UINT64_C(0xabcdef);
    assert(fixture_allocate(&gate, &owner, &allocator, FRAME_OWNER_KERNEL,
                            false, &output) == FRAME_FORBIDDEN);
    assert(output == UINT64_C(0xabcdef) && frame_allocator_check(&allocator));
    assert(gate_enter(&gate, &irq, &backend) == GATE_IRQ_CONTEXT);
    assert(gate_enter(&gate, &untrusted, &backend) == GATE_INVALID);
    assert(!gate.held && cpu.disable_calls == 0);

    assert(gate_enter(&gate, &owner, &backend) == GATE_OK);
    assert(!cpu.interrupts_enabled && cpu.disable_calls == 1);
    assert(gate_enter(&gate, &stale, &backend) == GATE_CONFLICT);
    assert(fixture_allocate(&gate, &stale, &allocator, FRAME_OWNER_KERNEL,
                            false, &output) == FRAME_FORBIDDEN);
    assert(fixture_allocate(&gate, &irq, &allocator, FRAME_OWNER_KERNEL,
                            false, &output) == FRAME_FORBIDDEN);
    assert(fixture_allocate(&gate, &untrusted, &allocator, FRAME_OWNER_KERNEL,
                            false, &output) == FRAME_FORBIDDEN);
    assert(output == UINT64_C(0xabcdef) && frame_allocator_check(&allocator));

    uint64_t pages[4];
    for (unsigned int i = 0; i < 4; ++i) {
        assert(fixture_allocate(&gate, &owner, &allocator, FRAME_OWNER_KERNEL,
                                false, &pages[i]) == FRAME_OK);
        assert(frame_allocator_check(&allocator));
    }
    /* The single reserved ordinal is never consumed by ordinary requests. */
    assert(pages[0] == 8192 && allocator.reserve_free == 1);
    output = UINT64_C(0xabcdef);
    struct frame_allocator before = allocator;
    uint8_t prior_slots[8];
    memcpy(prior_slots, slots, sizeof prior_slots);
    assert(fixture_allocate(&gate, &owner, &allocator, FRAME_OWNER_KERNEL,
                            false, &output) == FRAME_OOM);
    assert(output == UINT64_C(0xabcdef));
    assert(memcmp(&before, &allocator, sizeof before) == 0);
    assert(memcmp(prior_slots, slots, sizeof prior_slots) == 0);
    assert(fixture_allocate(&gate, &owner, &allocator, FRAME_OWNER_KERNEL,
                            true, &output) == FRAME_FORBIDDEN);
    for (unsigned int i = 0; i < 4; ++i) {
        assert(frame_release(&allocator, FRAME_OWNER_KERNEL, pages[i]) == FRAME_OK);
        assert(frame_allocator_check(&allocator));
    }
    assert(gate_exit(&gate, &owner, &backend) == GATE_OK);
    assert(!gate.held && cpu.restore_calls == 1 &&
           cpu.interrupts_enabled == initial_if);
    assert(gate_exit(&gate, &owner, &backend) != GATE_OK);
}

/* A failure prints its source line via assert; hosted sanitizers catch UB. */
int main(void)
{
    run_case(true);
    run_case(false);
    puts("SPIFFOS_M10_D59B_REAL_FRAME_HOSTED_OK");
    return 0;
}
