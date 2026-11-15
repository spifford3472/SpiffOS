/* Purpose: bounded nominal PIT/TSC ratio observation for QEMU diagnostics.
 * Dependencies: caller-provided read_tsc() and monotonic millisecond source.
 * Invariants: IF already enabled; no CLI/STI/allocation or output on failure.
 * Provenance: Qwen helper, coordinator overflow/rollback/C11 corrections.
 * Reference: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md. */
/* Borrow clock_ms and output for one bounded observation. IRQs are already
 * enabled; the diagnostic never changes IF. Ignore unavailable samples, reject
 * clock/TSC rollback or zero ratio, and preserve output on every failure.
 * Success publishes nominal guest cycles per millisecond, not a CPU guarantee.
 * Neither callback may allocate, mutate scheduler state or retain output. */
static bool calibrate(bool (*clock_ms)(uint64_t *), uint64_t *cycles_per_ms) {
    uint64_t start_ms, end_ms, start_tsc, end_tsc;
    uint64_t iter = 0;
    uint64_t last_ms = 0;

    if (!clock_ms || !cycles_per_ms) return false;

    // Wait for clock to become available.
    while (!clock_ms(&start_ms)) {
        if (++iter > 500000000) return false;
        __asm__ volatile("pause" ::: "memory");
    }

    start_tsc = read_tsc();
    last_ms = start_ms;

    // Poll until 200ms have passed or timeout.
    while (1) {
        if (++iter > 500000000) return false;
        __asm__ volatile("pause" ::: "memory");

        if (!clock_ms(&end_ms)) continue;

        // Check for rollback.
        if (end_ms < last_ms) return false;
        last_ms = end_ms;

        if (end_ms - start_ms >= 200u) break;
    }

    end_tsc = read_tsc();

    // Validate results.
    if (end_tsc < start_tsc || end_ms <= start_ms) return false;

    uint64_t elapsed_ms = end_ms - start_ms;
    if (elapsed_ms == 0) return false;

    uint64_t quotient = (end_tsc - start_tsc) / elapsed_ms;
    if (quotient == 0) return false;

    *cycles_per_ms = quotient;
    return true;
}
