/* Purpose: hosted-only D5.9-B exclusion state machine.
 * References: ADR-m10-d59b-allocator-exclusion-contract.md.
 * Invariants: trusted single CPU, off-IRQ, exact-generation ownership.
 * This file is NOT linked to production kernel builds. */
#include "gate.h"

#define GATE_MAX_NESTING 8u

/* Reject inconsistent or uninitialized gate state before mutation.
 * Does not authenticate origin; caller supplies trusted context. */
static bool gate_state_valid(const struct gate_state *state)
{
    return state->held ? (state->depth >= 1u && state->depth <= GATE_MAX_NESTING &&
                          state->owner.generation != 0u) : (state->depth == 0u);
}


enum gate_result gate_enter(struct gate_state *state,
                            const struct gate_context *context,
                            const struct gate_backend *backend)
{
    if (!state || !context || !backend ||
        !backend->interrupts_enabled || !backend->disable_interrupts ||
        !backend->restore_interrupts) {
        return GATE_INVALID;
    }

    if (context->owner.generation == 0u || !gate_state_valid(state)) {
        return GATE_INVALID;
    }

    if (context->in_irq) {
        return GATE_IRQ_CONTEXT;
    }

    if (!context->trusted) {
        return GATE_INVALID;
    }

    /* First admission: capture original IF before disabling interrupts. */
    if (!state->held) {
        state->saved_if = backend->interrupts_enabled(backend->opaque);
        backend->disable_interrupts(backend->opaque);
        state->owner = context->owner;
        state->depth = 1;
        state->held = true;
        return GATE_OK;
    }

    /* Only the exact same generation may nest into this gate. */
    if (state->owner.slot != context->owner.slot ||
        state->owner.generation != context->owner.generation) {
        return GATE_CONFLICT;
    }

    if (state->depth >= GATE_MAX_NESTING) {
        return GATE_OVERFLOW;
    }

    ++state->depth;
    return GATE_OK;
}

enum gate_result gate_exit(struct gate_state *state,
                           const struct gate_context *context,
                           const struct gate_backend *backend)
{
    if (!state || !context || !backend ||
        !backend->interrupts_enabled || !backend->disable_interrupts ||
        !backend->restore_interrupts) {
        return GATE_INVALID;
    }

    if (context->owner.generation == 0u || !gate_state_valid(state)) {
        return GATE_INVALID;
    }

    if (context->in_irq) {
        return GATE_IRQ_CONTEXT;
    }

    if (!context->trusted) {
        return GATE_INVALID;
    }

    /* Reject unmatched exit before touching the counter. */
    if (!state->held) {
        return GATE_CONFLICT;
    }

    /* Do not allow task-slot reuse to release stale gate ownership. */
    if (state->owner.slot != context->owner.slot ||
        state->owner.generation != context->owner.generation) {
        return GATE_NOT_OWNER;
    }

    /* Decrement only after validating nonzero and bounded nesting. */
    --state->depth;

    /* Restore precisely the original IF at the outermost exit. */
    if (state->depth == 0) {
        state->held = false;
        backend->restore_interrupts(backend->opaque, state->saved_if);
    }

    return GATE_OK;
}
