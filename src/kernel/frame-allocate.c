/* Purpose: allocate one owned physical frame from an explicit eligible pool.
 * Dependencies: initialized frame-allocator metadata and checked translation.
 * Invariants: exclusive boot CPU, IRQs disabled; no candidate frame access.
 * Reference: docs/decisions/ADR-frame-allocator.md.
 * Provenance: Codex fallback after the one-function Qwen job timed out. */
#include <stddef.h>
#include <stdint.h>
#include "frame-allocator.h"

/* Allocate one frame for a valid owner. Reserve requests require SUPERVISOR;
 * ordinary requests never fall back to reserve. Borrow all metadata exclusively.
 * Failure leaves descriptor/slots/output unchanged. address must not alias them.
 * No heap, frame contents, mappings, interrupts or concurrent operations. */
enum frame_result frame_allocate(struct frame_allocator *allocator,
    enum frame_owner owner, bool reserve_request, uint64_t *address)
{
    if (allocator == NULL || !allocator->initialized ||
        allocator->owners == NULL || allocator->regions == NULL ||
        address == NULL || !frame_owner_valid(owner) ||
        allocator->region_count == 0 ||
        allocator->region_count > MEMORY_MAP_MAX_ENTRIES ||
        allocator->reserve_frames > allocator->total_frames ||
        allocator->total_frames > allocator->slot_capacity) {
        return FRAME_INVALID;
    }
    if (reserve_request && owner != FRAME_OWNER_SUPERVISOR) {
        return FRAME_FORBIDDEN;
    }
    uint64_t first = reserve_request ? 0 : allocator->reserve_frames;
    uint64_t last = reserve_request ? allocator->reserve_frames :
        allocator->total_frames;
    uint64_t *free_count = reserve_request ?
        &allocator->reserve_free : &allocator->ordinary_free;
    if (*free_count > last - first) {
        return FRAME_INVALID;
    }
    if (*free_count == 0) {
        return FRAME_OOM;
    }
    for (uint64_t index = first; index < last; ++index) {
        if (allocator->owners[index] != FRAME_OWNER_FREE) {
            continue;
        }
        uint64_t selected;
        /* Translation can reject inconsistent metadata. Obtain the numeric
         * address before changing any slot or counter, preserving atomic error. */
        if (!frame_allocator_address(allocator, index, &selected)) {
            return FRAME_MAP_ERROR;
        }
        if (allocator->owned[owner] >= allocator->total_frames) {
            return FRAME_INVALID;
        }
        allocator->owners[index] = (uint8_t)owner;
        ++allocator->owned[owner];
        --*free_count;
        *address = selected;
        return FRAME_OK;
    }
    return FRAME_OOM;
}
