/* Purpose: implement pure authorization for access to one owned physical page.
 * Responsibilities: revalidate M1/M2 metadata, find one complete candidate
 * frame, check its owner, and validate direct-map arithmetic/canonicality.
 * Dependencies: page-access.h, M1 memory-map analysis and M2 ownership slots.
 * Invariants: no numeric address is dereferenced here; only an injected,
 * synchronous backend may act on a successfully authorized mapped address.
 * Reference: docs/decisions/ADR-memory-services.md, M3 page-access contract. */
#include <stddef.h>
#include <stdint.h>
#include "page-access.h"

/* Describe one nonempty half-open object-address range without overflowing.
 * Numeric comparisons are used only to reject output aliasing; this helper
 * never dereferences, orders or subtracts unrelated C pointers. */
static bool page_access_object_range(uintptr_t start, size_t length,
    uintptr_t *end)
{
    if (length == 0 || length > UINTPTR_MAX - start) {
        return false;
    }
    *end = start + length;
    return true;
}

/* Report whether one uint64_t output object is disjoint from every borrowed
 * policy/allocator metadata range that resolution relies on. The allocator was
 * already checked, so the used region and ownership-array byte counts fit in
 * size_t. Rejecting aliases before publication prevents an otherwise valid
 * result from corrupting the authority that justified it. Pure numeric checks;
 * no metadata, frame contents or output bytes are changed. */
static bool page_access_output_disjoint(const struct page_access_policy *policy,
    const uint64_t *output)
{
    const struct frame_allocator *allocator = policy->allocator;
    uintptr_t output_start = (uintptr_t)output;
    uintptr_t ranges_start[4] = {
        (uintptr_t)policy,
        (uintptr_t)allocator,
        (uintptr_t)allocator->regions,
        (uintptr_t)allocator->owners
    };
    size_t ranges_length[4] = {
        sizeof *policy,
        sizeof *allocator,
        allocator->region_count * sizeof *allocator->regions,
        (size_t)allocator->total_frames * sizeof *allocator->owners
    };
    uintptr_t output_end;
    if (!page_access_object_range(output_start, sizeof *output, &output_end)) {
        return false;
    }
    for (size_t index = 0; index < 4; ++index) {
        uintptr_t range_end;
        if (!page_access_object_range(ranges_start[index],
                                      ranges_length[index], &range_end) ||
            (output_start < range_end && ranges_start[index] < output_end)) {
            return false;
        }
    }
    return true;
}

/* Find the unique M2 ordinal for one aligned physical candidate frame.
 * allocator and ordinal are borrowed for the call; the caller has already
 * validated the complete M1 map and proved total_frames matches that map.
 * Success writes ordinal. Failure preserves it. No frame contents are read. */
static bool page_access_find_ordinal(const struct frame_allocator *allocator,
    uint64_t physical_address, uint64_t *ordinal)
{
    uint64_t physical_frame = physical_address / MEMORY_PAGE_BYTES;
    uint64_t prefix = 0;

    for (size_t index = 0; index < allocator->region_count; ++index) {
        const struct memory_region *region = &allocator->regions[index];
        if (region->type != MEMORY_REGION_USABLE || region->length == 0) {
            continue;
        }

        /* memory_map_analyze() already proved base + length cannot overflow.
         * Frame-number arithmetic avoids an overflowing align-up expression. */
        uint64_t first_frame = region->base / MEMORY_PAGE_BYTES;
        if (region->base % MEMORY_PAGE_BYTES != 0) {
            ++first_frame;
        }
        if (first_frame == 0) {
            first_frame = 1;
        }
        uint64_t last_frame =
            (region->base + region->length) / MEMORY_PAGE_BYTES;
        uint64_t span = last_frame > first_frame ?
            last_frame - first_frame : 0;

        if (physical_frame >= first_frame && physical_frame < last_frame) {
            uint64_t found = prefix + physical_frame - first_frame;
            if (found >= allocator->total_frames) {
                return false;
            }
            *ordinal = found;
            return true;
        }
        /* The analyzed candidate total bounds every accumulated span. */
        if (span > allocator->total_frames - prefix) {
            return false;
        }
        prefix += span;
    }
    return false;
}

/* Report whether one numeric x86-64 virtual address is canonical for the
 * selected paging width. The accepted widths are checked by the caller.
 * Pure arithmetic only; no pointer conversion, memory access or side effects. */
static bool page_access_is_canonical(uint64_t address, uint8_t address_bits)
{
    /* An N-bit canonical address sign-extends bit N-1. Therefore the low
     * interval ends at 2^(N-1)-1 and the high interval starts where all upper
     * bits are one; addresses strictly between those bounds are the hole. */
    uint64_t low_max = (UINT64_C(1) << (address_bits - 1)) - 1;
    uint64_t high_min = UINT64_MAX - low_max;
    return address <= low_max || address >= high_min;
}

/* Resolve one owned candidate page into an authorized numeric direct-map
 * address. All metadata is borrowed/read-only and must remain stable. Success
 * publishes once; every error preserves output and allocator state. This is
 * boot-CPU policy code: no allocation, logging, pointer access, locks or IRQs. */
enum page_access_result page_access_resolve(
    const struct page_access_policy *policy, enum frame_owner owner,
    uint64_t physical_address, uint64_t *mapped_address)
{
    if (policy == NULL || mapped_address == NULL ||
        !frame_owner_valid(owner) || policy->allocator == NULL) {
        return PAGE_ACCESS_INVALID;
    }
    const struct frame_allocator *allocator = policy->allocator;
    if (!frame_allocator_check(allocator)) {
        return PAGE_ACCESS_INVALID;
    }
    if (!page_access_output_disjoint(policy, mapped_address)) {
        return PAGE_ACCESS_INVALID;
    }

    struct memory_inventory inventory;
    if (memory_map_analyze(allocator->regions, allocator->region_count,
                           &inventory) != MEMORY_MAP_OK ||
        inventory.candidate_frames != allocator->total_frames) {
        return PAGE_ACCESS_INVALID;
    }
    if (policy->virtual_address_bits != PAGE_ACCESS_X86_64_4_LEVEL_BITS &&
        policy->virtual_address_bits != PAGE_ACCESS_X86_64_5_LEVEL_BITS) {
        return PAGE_ACCESS_UNSUPPORTED;
    }
    if (physical_address == 0 ||
        physical_address % MEMORY_PAGE_BYTES != 0) {
        return PAGE_ACCESS_BAD_PHYSICAL_ADDRESS;
    }
    /* Require the exclusive end itself to fit, not merely the last byte. */
    if (physical_address > UINT64_MAX - MEMORY_PAGE_BYTES) {
        return PAGE_ACCESS_ADDRESS_OVERFLOW;
    }

    uint64_t ordinal;
    if (!page_access_find_ordinal(allocator, physical_address, &ordinal)) {
        return PAGE_ACCESS_BAD_PHYSICAL_ADDRESS;
    }
    if (allocator->owners[ordinal] != (uint16_t)owner) {
        return PAGE_ACCESS_WRONG_OWNER;
    }
    if (physical_address > UINT64_MAX - policy->direct_map_offset) {
        return PAGE_ACCESS_ADDRESS_OVERFLOW;
    }
    uint64_t resolved = physical_address + policy->direct_map_offset;
    if (resolved > UINT64_MAX - MEMORY_PAGE_BYTES) {
        return PAGE_ACCESS_ADDRESS_OVERFLOW;
    }
    uint64_t resolved_last = resolved + (MEMORY_PAGE_BYTES - 1);
    if (!page_access_is_canonical(resolved, policy->virtual_address_bits) ||
        !page_access_is_canonical(resolved_last,
                                  policy->virtual_address_bits)) {
        return PAGE_ACCESS_NONCANONICAL;
    }

    *mapped_address = resolved;
    return PAGE_ACCESS_OK;
}

/* Authorize a page and synchronously ask the injected backend to clear exactly
 * one frame. The backend/context remain caller-owned. Failure publishes no
 * address and changes no allocator metadata; a failing backend may have
 * partially changed its private target, so higher layers must roll ownership
 * back before reuse. Boot CPU only, interrupts disabled, no locks/allocation. */
enum page_access_result page_access_zero(
    const struct page_access_policy *policy, enum frame_owner owner,
    uint64_t physical_address, page_access_zero_backend backend,
    void *backend_context)
{
    if (backend == NULL) {
        return PAGE_ACCESS_INVALID;
    }
    uint64_t mapped_address;
    enum page_access_result result = page_access_resolve(
        policy, owner, physical_address, &mapped_address);
    if (result != PAGE_ACCESS_OK) {
        return result;
    }
    if (!backend(backend_context, mapped_address, MEMORY_PAGE_BYTES)) {
        return PAGE_ACCESS_BACKEND_FAILED;
    }
    return PAGE_ACCESS_OK;
}

/* Allocate one ordinary page and zero it before publishing the physical
 * address. The exact owner acquired below is also used for rollback, so a
 * failed initialization cannot leak a live ownership slot. Output is written
 * only after zeroing succeeds. Borrow all state exclusively and synchronously;
 * boot CPU only, interrupts disabled, no locks or heap allocation. */
enum page_access_result page_access_allocate_zero(
    struct frame_allocator *allocator, const struct page_access_policy *policy,
    enum frame_owner owner, page_access_zero_backend backend,
    void *backend_context, uint64_t *physical_address)
{
    if (allocator == NULL || policy == NULL ||
        policy->allocator != allocator || backend == NULL ||
        physical_address == NULL || !frame_owner_valid(owner)) {
        return PAGE_ACCESS_INVALID;
    }
    /* Allocation is the first mutation in this operation. Independently prove
     * M2 counters/owners and output separation before allowing it, rather than
     * relying on the later resolver to discover corruption after acquisition. */
    if (!frame_allocator_check(allocator) ||
        !page_access_output_disjoint(policy, physical_address)) {
        return PAGE_ACCESS_INVALID;
    }

    uint64_t allocated_address;
    if (frame_allocate(allocator, owner, false, &allocated_address) != FRAME_OK) {
        return PAGE_ACCESS_ALLOCATION_FAILED;
    }
    enum page_access_result result = page_access_zero(
        policy, owner, allocated_address, backend, backend_context);
    if (result != PAGE_ACCESS_OK) {
        if (frame_release(allocator, owner, allocated_address) != FRAME_OK) {
            return PAGE_ACCESS_ROLLBACK_FAILED;
        }
        return result;
    }
    *physical_address = allocated_address;
    return PAGE_ACCESS_OK;
}
