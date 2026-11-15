/* Purpose: isolated hosted D5.9-B single-CPU allocator transaction gate.
 * Dependencies: stdbool/stdint only; caller-owned backend and state.
 * Invariant: no IRQ invocation, no production allocator integration.
 * References: ADR-m10-d59b-allocator-exclusion-contract.md. */
#ifndef GATE_H
#define GATE_H

#include <stdbool.h>
#include <stdint.h>

/* Explicit results: reject bad admission without state mutation. */
enum gate_result {
    GATE_OK,
    GATE_INVALID,
    GATE_IRQ_CONTEXT,
    GATE_CONFLICT,
    GATE_OVERFLOW,
    GATE_NOT_OWNER
};

/* Task slot and nonzero exact generation, supplied by trusted scheduler. */
struct gate_owner {
    uint16_t slot;
    uint32_t generation;
};

/* Trusted off-IRQ provenance supplied by caller; not self-authenticating. */
struct gate_context {
    bool trusted;
    bool in_irq;
    struct gate_owner owner;
};

/* Mockable interrupt backend: never substitute blind STI for restoration. */
struct gate_backend {
    bool (*interrupts_enabled)(void*);
    void (*disable_interrupts)(void*);
    void (*restore_interrupts)(void*, bool);
    void *opaque;
};

/* Caller owns this state; initialize to all zero before first use. */
struct gate_state {
    bool held;
    struct gate_owner owner;
    uint16_t depth;
    bool saved_if;
};

/* Enter with exact task identity; preserve initial IF at outermost entry.
 * Returns GATE_OK or a rejected admission, with state unchanged on error.
 * No blocking, allocation, IRQ context or concurrent multi-CPU access. */
enum gate_result gate_enter(struct gate_state *state,
                            const struct gate_context *context,
                            const struct gate_backend *backend);

/* Exit only as the exact owning task; final exit restores initial IF.
 * No blocking, allocation, IRQ context or concurrent multi-CPU access. */
enum gate_result gate_exit(struct gate_state *state,
                           const struct gate_context *context,
                           const struct gate_backend *backend);

#endif /* GATE_H */
