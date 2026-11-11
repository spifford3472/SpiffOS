/* Purpose: initialize, translate, verify and release owned physical frames.
 * Dependencies: M1 pure inventory and caller-owned metadata; no frame contents.
 * Invariants: exclusive boot CPU, interrupts disabled, no heap or mappings.
 * Reference: docs/decisions/ADR-frame-allocator.md (internal diagnostic API). */
#include <stddef.h>
#include <stdint.h>
#include "frame-allocator.h"

/* Classify a diagnostic owner token; pure, no side effects or ownership. */
bool frame_owner_valid(enum frame_owner owner)
{
    return owner >= FRAME_OWNER_KERNEL && owner < FRAME_OWNER_COUNT;
}

/* Find a usable region's half-open frame-number interval. False means no
 * complete candidate frames or invalid arithmetic. Borrow immutable metadata;
 * output pointers are internal non-NULL stack variables. No frame access. */
static bool region_frames(const struct memory_region *region,
    uint64_t *first, uint64_t *last)
{
    if (region->type != MEMORY_REGION_USABLE || region->length == 0 ||
        region->length > UINT64_MAX - region->base) {
        return false;
    }
    *first = region->base / MEMORY_PAGE_BYTES +
        (region->base % MEMORY_PAGE_BYTES != 0);
    if (*first == 0) {
        *first = 1;
    }
    *last = (region->base + region->length) / MEMORY_PAGE_BYTES;
    return *last > *first;
}

/* Validate the complete map and metadata budget before clearing any slots.
 * All state is borrowed/exclusive and nonaliasing; the descriptor begins zeroed.
 * No physical-frame writes or heap allocation; failure preserves prior bytes. */
enum frame_result frame_allocator_initialize(struct frame_allocator *allocator,
    const struct memory_region *regions, size_t count, uint8_t *slots,
    size_t slot_capacity, uint64_t reserve_frames)
{
    if (allocator == NULL || slots == NULL || allocator->initialized) {
        return FRAME_INVALID;
    }
    struct memory_inventory inventory;
    if (memory_map_analyze(regions, count, &inventory) != MEMORY_MAP_OK) {
        return FRAME_MAP_ERROR;
    }
    if (inventory.candidate_frames > slot_capacity ||
        inventory.candidate_frames > SIZE_MAX / sizeof *slots) {
        return FRAME_CAPACITY;
    }
    if (reserve_frames > inventory.candidate_frames) {
        return FRAME_INVALID;
    }
    struct frame_allocator next = {
        .regions = regions, .region_count = count, .owners = slots,
        .slot_capacity = slot_capacity,
        .total_frames = inventory.candidate_frames,
        .reserve_frames = reserve_frames,
        .ordinary_free = inventory.candidate_frames - reserve_frames,
        .reserve_free = reserve_frames, .initialized = true
    };
    for (uint64_t index = 0; index < inventory.candidate_frames; ++index) {
        slots[index] = FRAME_OWNER_FREE;
    }
    *allocator = next;
    return FRAME_OK;
}

/* Translate an ordinal across usable-region holes without dereferencing the
 * resulting physical address. Caller keeps the map immutable after init.
 * Reject invalid inputs without modifying the borrowed address output. */
bool frame_allocator_address(const struct frame_allocator *allocator,
    uint64_t ordinal, uint64_t *address)
{
    if (allocator == NULL || !allocator->initialized || address == NULL ||
        ordinal >= allocator->total_frames || allocator->regions == NULL ||
        allocator->region_count == 0 ||
        allocator->region_count > MEMORY_MAP_MAX_ENTRIES) {
        return false;
    }
    for (size_t region = 0; region < allocator->region_count; ++region) {
        uint64_t first, last;
        if (!region_frames(&allocator->regions[region], &first, &last)) {
            continue;
        }
        uint64_t span = last - first;
        if (ordinal < span) {
            /* first+ordinal < last <= UINT64_MAX/4096; multiply is safe. */
            *address = (first + ordinal) * MEMORY_PAGE_BYTES;
            return true;
        }
        ordinal -= span;
    }
    return false;
}

/* Translate one aligned nonzero physical address back to its unique ordinal.
 * Whole-map validation at init rules out usable overlaps. Borrow stable
 * metadata and a non-NULL stack output; no physical-memory reads or writes. */
static bool address_ordinal(const struct frame_allocator *allocator,
    uint64_t address, uint64_t *ordinal)
{
    if (address == 0 || address % MEMORY_PAGE_BYTES != 0) {
        return false;
    }
    uint64_t frame = address / MEMORY_PAGE_BYTES;
    uint64_t prefix = 0;
    for (size_t region = 0; region < allocator->region_count; ++region) {
        uint64_t first, last;
        if (!region_frames(&allocator->regions[region], &first, &last)) {
            continue;
        }
        uint64_t span = last - first;
        if (span > allocator->total_frames - prefix) {
            return false;
        }
        if (frame >= first && frame < last) {
            *ordinal = prefix + frame - first;
            return true;
        }
        prefix += span;
    }
    return false;
}

/* Free only a currently allocated frame with the exact matching owner.
 * Validate before mutation; the ordinal retains its reserve/ordinary identity.
 * Borrow exclusive metadata; no frame access, heap, locks or interrupts. */
enum frame_result frame_release(struct frame_allocator *allocator,
    enum frame_owner owner, uint64_t address)
{
    if (allocator == NULL || !allocator->initialized ||
        allocator->owners == NULL || allocator->regions == NULL ||
        allocator->region_count == 0 ||
        allocator->region_count > MEMORY_MAP_MAX_ENTRIES ||
        !frame_owner_valid(owner) ||
        allocator->reserve_frames > allocator->total_frames ||
        allocator->total_frames > allocator->slot_capacity) {
        return FRAME_INVALID;
    }
    uint64_t ordinal;
    if (!address_ordinal(allocator, address, &ordinal)) {
        return FRAME_BAD_ADDRESS;
    }
    uint8_t stored_owner = allocator->owners[ordinal];
    if (stored_owner == FRAME_OWNER_FREE) {
        return FRAME_DOUBLE_FREE;
    }
    if (stored_owner != (uint8_t)owner) {
        return FRAME_WRONG_OWNER;
    }
    bool reserved = ordinal < allocator->reserve_frames;
    uint64_t *free_count = reserved ?
        &allocator->reserve_free : &allocator->ordinary_free;
    uint64_t pool_size = reserved ? allocator->reserve_frames :
        allocator->total_frames - allocator->reserve_frames;
    if (allocator->owned[owner] == 0 || *free_count >= pool_size ||
        (reserved && owner != FRAME_OWNER_SUPERVISOR)) {
        return FRAME_INVALID;
    }
    allocator->owners[ordinal] = FRAME_OWNER_FREE;
    --allocator->owned[owner];
    ++*free_count;
    return FRAME_OK;
}

/* Independently scan every used ownership slot and compare all counters.
 * Caller guarantees borrowed storage is valid and unchanged. Runtime cost is
 * bounded by initialized capacity; no candidate-frame access or allocation. */
bool frame_allocator_check(const struct frame_allocator *allocator)
{
    if (allocator == NULL || !allocator->initialized ||
        allocator->owners == NULL || allocator->regions == NULL ||
        allocator->region_count == 0 ||
        allocator->region_count > MEMORY_MAP_MAX_ENTRIES ||
        allocator->reserve_frames > allocator->total_frames ||
        allocator->total_frames > allocator->slot_capacity ||
        allocator->total_frames > SIZE_MAX / sizeof *allocator->owners) {
        return false;
    }
    uint64_t live[FRAME_OWNER_COUNT] = {0};
    uint64_t ordinary_free = 0, reserve_free = 0;
    for (uint64_t index = 0; index < allocator->total_frames; ++index) {
        uint8_t owner = allocator->owners[index];
        bool reserved = index < allocator->reserve_frames;
        if (owner == FRAME_OWNER_FREE) {
            if (reserved) ++reserve_free;
            else ++ordinary_free;
        } else {
            if (!frame_owner_valid((enum frame_owner)owner) ||
                (reserved && owner != FRAME_OWNER_SUPERVISOR)) {
                return false;
            }
            ++live[owner];
        }
    }
    if (ordinary_free != allocator->ordinary_free ||
        reserve_free != allocator->reserve_free) {
        return false;
    }
    for (unsigned int owner = 0; owner < FRAME_OWNER_COUNT; ++owner) {
        if (live[owner] != allocator->owned[owner]) {
            return false;
        }
    }
    return true;
}
