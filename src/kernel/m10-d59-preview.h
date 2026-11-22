/* Purpose: isolated preview scheduler interface for the foreground task.
 * Dependencies: D5.6 IRQ fixture and D5.7/D5.9-B guard.
 * Invariants: exactly one foreground owner, one CPU and no IRQ callers.
 * Reference: docs/decisions/ADR-m10-d59-multitasking-preview.md. */
#ifndef SPIFFOS_M10_D59_PREVIEW_H
#define SPIFFOS_M10_D59_PREVIEW_H
#include <stdbool.h>
#include <stdint.h>

/* All counters are sampled together. Durations are measured in IRQ0 ticks. */
struct m10_d59_preview_metrics {
    uint32_t ticks;
    uint32_t worker_ticks[2];
    uint32_t foreground_ticks;
    uint32_t irq_switches;
    uint64_t supervisor_completions;
    uint64_t max_supervisor_gap;
    uint64_t worker_iterations[2];
};

bool m10_d59_preview_snapshot(struct m10_d59_preview_metrics *out);
#endif
