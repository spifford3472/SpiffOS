/* Purpose: pure read-only inventory of physical-memory-map intervals.
 * Dependencies: memory-map.h and borrowed caller-owned metadata.
 * Invariants: no allocation, frame access, hardware or concurrent mutation.
 * Reference: docs/decisions/ADR-memory-foundation.md (M1 only).
 * Provenance: Qwen initial draft; Codex corrected validation/atomic arithmetic. */
#include <stddef.h>
#include <stdint.h>
#include "memory-map.h"

/* Validate all intervals and count complete candidate frames. Inputs/summary
 * must be readable/writable respectively, nonaliasing and stable for this call.
 * Failure changes neither; success publishes the local result exactly once.
 * Frame numbers, not physical addresses, are used for inward alignment so the
 * calculation never needs an overflowing address + 4095 expression.
 * Boot CPU only, no interrupts, allocation or physical-memory access. */
enum memory_map_result memory_map_analyze(const struct memory_region *regions,
    size_t count, struct memory_inventory *summary)
{
    if (regions == NULL || summary == NULL || count == 0) {
        return MEMORY_MAP_INVALID;
    }
    if (count > MEMORY_MAP_MAX_ENTRIES) {
        return MEMORY_MAP_TOO_MANY;
    }
    /* Check every exclusive end before any pair comparison can use it.
     * Subtraction checks the bound without evaluating an overflowing sum. */
    for (size_t index = 0; index < count; ++index) {
        if (regions[index].length > UINT64_MAX - regions[index].base) {
            return MEMORY_MAP_OVERFLOW;
        }
    }
    for (size_t index = 0; index < count; ++index) {
        const struct memory_region *left = &regions[index];
        if (left->length == 0) {
            continue;
        }
        uint64_t left_end = left->base + left->length;
        for (size_t other = index + 1; other < count; ++other) {
            const struct memory_region *right = &regions[other];
            if (right->length == 0 ||
                (left->type != MEMORY_REGION_USABLE &&
                 right->type != MEMORY_REGION_USABLE)) {
                continue;
            }
            uint64_t right_end = right->base + right->length;
            /* Half-open adjacent intervals do not overlap. Nonusable-only
             * overlaps are legitimate protocol metadata and are not summed. */
            if (left->base < right_end && right->base < left_end) {
                return MEMORY_MAP_OVERLAP;
            }
        }
    }

    struct memory_inventory result = {0};
    for (size_t index = 0; index < count; ++index) {
        const struct memory_region *region = &regions[index];
        if (region->type != MEMORY_REGION_USABLE || region->length == 0) {
            continue;
        }
        if (region->length > UINT64_MAX - result.usable_bytes) {
            return MEMORY_MAP_OVERFLOW;
        }
        result.usable_bytes += region->length;
        uint64_t first_frame = region->base / MEMORY_PAGE_BYTES;
        if (region->base % MEMORY_PAGE_BYTES != 0) {
            /* The quotient is at most UINT64_MAX/4096, so +1 is safe. */
            ++first_frame;
        }
        if (first_frame == 0) {
            first_frame = 1; /* Explicit M1 exclusion of physical frame zero. */
        }
        uint64_t last_frame =
            (region->base + region->length) / MEMORY_PAGE_BYTES;
        if (last_frame > first_frame) {
            uint64_t frames = last_frame - first_frame;
            if (frames > UINT64_MAX - result.candidate_frames) {
                return MEMORY_MAP_OVERFLOW;
            }
            result.candidate_frames += frames;
        }
    }
    if (result.candidate_frames > UINT64_MAX / MEMORY_PAGE_BYTES) {
        return MEMORY_MAP_OVERFLOW;
    }
    result.candidate_bytes = result.candidate_frames * MEMORY_PAGE_BYTES;
    if (result.candidate_bytes > result.usable_bytes) {
        return MEMORY_MAP_OVERFLOW;
    }
    /* This identity includes short fragments and exactly-one-page frame zero;
     * no early continue can accidentally omit excluded usable bytes. */
    result.excluded_usable_bytes = result.usable_bytes - result.candidate_bytes;
    *summary = result;
    return MEMORY_MAP_OK;
}
