/* Purpose: allocate one owned physical frame from an explicit eligible pool.
 * Dependencies: initialized frame-allocator metadata and checked translation.
 * Invariants: exclusive boot CPU, IRQs disabled; no candidate frame access.
 * Reference: docs/decisions/ADR-frame-allocator.md.
 * Provenance: Codex fallback after the one-function Qwen job timed out. */
#include <stddef.h>
#include <stdint.h>
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
#include "m10-d59b-context.h"
/* One authoritative body, renamed only in this translation unit. Public
 * entries below authenticate before inspecting its borrowed metadata.
 * Same-file internal calls stay inside their already-admitted outer scope. */
#define frame_allocate d59b_private_frame_allocate
#endif
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
#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
        /* Retained/quarantined binding still owns backing even if a corrupt
         * or partially retired M2 slot appears FREE. Do not recycle it. */
        if(d59b_memory_frame_bound(selected)) continue;
#endif
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

#ifdef SPIFFOS_M10_D59B_MEMORY_WIRING_TEST
#undef frame_allocate

/* Preserve the original API input/output/ownership contract. Authenticate
 * actual boot/task execution before calling the SAME existing body; D5.7
 * is the sole runtime guard. No output staging, retry or alternate allocator.
 * An uncertain exit stops IF-off after possible publication. */
enum frame_result frame_allocate(struct frame_allocator *allocator, enum frame_owner owner, bool reserve_request, uint64_t *address)
{
    struct d59b_memory_scope scope={0};
    if(!d59b_memory_scope_enter(&scope)) return FRAME_INVALID;
    /* Only the measured <=512MiB tracking profile is admitted at runtime.
     * Bootstrap still handles its original capacity/unavailable contract. */
    if(scope.context!=NULL && allocator!=NULL &&
       allocator->total_frames>512u*1024u*1024u/MEMORY_PAGE_BYTES) {
        if(!d59b_memory_scope_leave(&scope)) d59b_memory_stop();
        return FRAME_INVALID;
    }
    enum frame_result result=d59b_private_frame_allocate(allocator,owner,reserve_request,address);
    if(!d59b_memory_scope_leave(&scope)) d59b_memory_stop();
    return result;
}
#endif
