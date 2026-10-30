/* Purpose: implement bounded initialization and allocation for the M4 heap.
 * Responsibilities: validate caller-owned storage, maintain an externally
 * described address-ordered block list, split by first fit and clear allocations.
 * Dependencies: kernel-heap.h and ordinary byte stores; the caller must supply
 * a contiguous arena assembled from M3-validated, kernel-owned ordinary pages.
 * Invariants: no metadata lives in the arena; failures preserve all storage;
 * supervisor-reserve pages are never requested or discovered by this module.
 * Reference: docs/decisions/ADR-memory-services.md, M4 bounded heap contract. */
#include <stddef.h>
#include <stdint.h>
#include "kernel-heap.h"

/* Describe one nonempty half-open host-address range without performing an
 * overflowing pointer addition. start/length are numeric validation values;
 * this helper neither dereferences nor takes ownership of the named storage. */
static bool kernel_heap_range(uintptr_t start, size_t length, uintptr_t *end)
{
    if (length == 0 || length > UINTPTR_MAX - start) {
        return false;
    }
    *end = start + length;
    return true;
}

/* Report whether two already validated half-open numeric ranges overlap.
 * Pure comparison only; adjacent storage is permitted and no pointer ordering
 * is used, avoiding C's restrictions on comparing unrelated object pointers. */
static bool kernel_heap_ranges_overlap(uintptr_t first_start,
    uintptr_t first_end, uintptr_t second_start, uintptr_t second_end)
{
    return first_start < second_end && second_start < first_end;
}

/* Validate arena, record-array and descriptor address ranges before any field
 * or record is changed. The caller retains all storage. False has no side
 * effects and means initialization cannot safely establish disjoint metadata. */
static bool kernel_heap_storage_valid(const struct kernel_heap *heap,
    const void *arena, size_t arena_bytes,
    const struct kernel_heap_block *blocks, size_t block_capacity)
{
    if (heap == NULL || arena == NULL || blocks == NULL || arena_bytes == 0 ||
        arena_bytes > KERNEL_HEAP_MAX_ARENA_BYTES ||
        arena_bytes % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        (uintptr_t)arena % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        block_capacity == 0 || block_capacity > KERNEL_HEAP_MAX_BLOCKS ||
        block_capacity > SIZE_MAX / sizeof *blocks ||
        (uintptr_t)blocks % _Alignof(struct kernel_heap_block) != 0) {
        return false;
    }

    uintptr_t heap_start = (uintptr_t)heap;
    uintptr_t arena_start = (uintptr_t)arena;
    uintptr_t blocks_start = (uintptr_t)blocks;
    uintptr_t heap_end, arena_end, blocks_end;
    size_t blocks_bytes = block_capacity * sizeof *blocks;
    if (!kernel_heap_range(heap_start, sizeof *heap, &heap_end) ||
        !kernel_heap_range(arena_start, arena_bytes, &arena_end) ||
        !kernel_heap_range(blocks_start, blocks_bytes, &blocks_end)) {
        return false;
    }
    return !kernel_heap_ranges_overlap(heap_start, heap_end,
                                       arena_start, arena_end) &&
        !kernel_heap_ranges_overlap(heap_start, heap_end,
                                    blocks_start, blocks_end) &&
        !kernel_heap_ranges_overlap(arena_start, arena_end,
                                    blocks_start, blocks_end);
}

/* Require the documented zeroed, unused descriptor state. Every named field is
 * checked; structure padding is deliberately not read. Pure and read-only. */
static bool kernel_heap_descriptor_zeroed(const struct kernel_heap *heap)
{
    if (heap->arena != NULL || heap->arena_bytes != 0 || heap->blocks != NULL ||
        heap->block_capacity != 0 || heap->first_block_index != 0 ||
        heap->active_blocks != 0 || heap->allocated_bytes != 0 ||
        heap->free_bytes != 0 || heap->live_allocations != 0 ||
        heap->initialized) {
        return false;
    }
    for (size_t owner = 0; owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        if (heap->owner_bytes[owner] != 0 ||
            heap->owner_allocations[owner] != 0) {
            return false;
        }
    }
    return true;
}

/* Validate that the pointer-sized output object cannot be cleared as payload or
 * overwritten as metadata during allocation. All heap ranges were validated by
 * kernel_heap_check() before this helper is called. Pure numeric checks only. */
static bool kernel_heap_output_disjoint(const struct kernel_heap *heap,
    void **allocation)
{
    uintptr_t output_start = (uintptr_t)allocation;
    uintptr_t heap_start = (uintptr_t)heap;
    uintptr_t arena_start = (uintptr_t)heap->arena;
    uintptr_t blocks_start = (uintptr_t)heap->blocks;
    uintptr_t output_end, heap_end, arena_end, blocks_end;
    size_t blocks_bytes = heap->block_capacity * sizeof *heap->blocks;
    return kernel_heap_range(output_start, sizeof *allocation, &output_end) &&
        kernel_heap_range(heap_start, sizeof *heap, &heap_end) &&
        kernel_heap_range(arena_start, heap->arena_bytes, &arena_end) &&
        kernel_heap_range(blocks_start, blocks_bytes, &blocks_end) &&
        !kernel_heap_ranges_overlap(output_start, output_end,
                                    heap_start, heap_end) &&
        !kernel_heap_ranges_overlap(output_start, output_end,
                                    arena_start, arena_end) &&
        !kernel_heap_ranges_overlap(output_start, output_end,
                                    blocks_start, blocks_end);
}

/* Validate the complete active graph and independently recompute accounting.
 * All descriptor/record/arena storage is borrowed read-only. Arena payload is
 * never inspected. False changes nothing and marks the heap unsafe to mutate. */
bool kernel_heap_check(const struct kernel_heap *heap)
{
    if (heap == NULL || !heap->initialized ||
        !kernel_heap_storage_valid(heap, heap->arena, heap->arena_bytes,
                                   heap->blocks, heap->block_capacity) ||
        heap->active_blocks == 0 ||
        heap->active_blocks > heap->block_capacity ||
        heap->first_block_index >= heap->block_capacity) {
        return false;
    }

    size_t active_slots = 0;
    for (size_t index = 0; index < heap->block_capacity; ++index) {
        if (heap->blocks[index].slot_active) {
            ++active_slots;
        }
    }
    if (active_slots != heap->active_blocks) {
        return false;
    }

    size_t owner_bytes[KERNEL_HEAP_OWNER_COUNT] = {0};
    size_t owner_allocations[KERNEL_HEAP_OWNER_COUNT] = {0};
    size_t allocated_bytes = 0;
    size_t free_bytes = 0;
    size_t live_allocations = 0;
    size_t expected_offset = 0;
    size_t previous_index = SIZE_MAX;
    size_t current_index = heap->first_block_index;
    bool previous_was_free = false;

    for (size_t visited = 0; visited < heap->active_blocks; ++visited) {
        if (current_index >= heap->block_capacity) {
            return false;
        }
        const struct kernel_heap_block *block = &heap->blocks[current_index];
        if (!block->slot_active || block->previous_index != previous_index ||
            block->offset_bytes != expected_offset || block->length_bytes == 0 ||
            block->offset_bytes % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
            block->length_bytes % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
            block->offset_bytes > heap->arena_bytes ||
            block->length_bytes > heap->arena_bytes - block->offset_bytes) {
            return false;
        }

        if (block->allocated) {
            if (block->owner < KERNEL_HEAP_OWNER_KERNEL ||
                block->owner >= KERNEL_HEAP_OWNER_COUNT ||
                owner_bytes[block->owner] > SIZE_MAX - block->length_bytes ||
                allocated_bytes > SIZE_MAX - block->length_bytes ||
                owner_allocations[block->owner] == SIZE_MAX ||
                live_allocations == SIZE_MAX) {
                return false;
            }
            owner_bytes[block->owner] += block->length_bytes;
            ++owner_allocations[block->owner];
            allocated_bytes += block->length_bytes;
            ++live_allocations;
            previous_was_free = false;
        } else {
            if (block->owner != KERNEL_HEAP_OWNER_FREE || previous_was_free ||
                free_bytes > SIZE_MAX - block->length_bytes) {
                return false;
            }
            free_bytes += block->length_bytes;
            previous_was_free = true;
        }

        expected_offset += block->length_bytes;
        size_t next_index = block->next_index;
        if (visited + 1 == heap->active_blocks) {
            if (next_index != SIZE_MAX) {
                return false;
            }
        } else {
            if (next_index >= heap->block_capacity ||
                heap->blocks[next_index].previous_index != current_index) {
                return false;
            }
        }
        previous_index = current_index;
        current_index = next_index;
    }

    if (expected_offset != heap->arena_bytes ||
        allocated_bytes != heap->allocated_bytes ||
        free_bytes != heap->free_bytes ||
        live_allocations != heap->live_allocations ||
        allocated_bytes > heap->arena_bytes ||
        free_bytes != heap->arena_bytes - allocated_bytes) {
        return false;
    }
    for (size_t owner = 0; owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        if (owner_bytes[owner] != heap->owner_bytes[owner] ||
            owner_allocations[owner] != heap->owner_allocations[owner]) {
            return false;
        }
    }
    return true;
}

/* Initialize one bounded heap without obtaining or touching backing pages.
 * The caller supplies a zeroed descriptor and disjoint, writable storage whose
 * ownership/mapping was validated before this call. Validation completes before
 * record writes; success borrows storage for the heap lifetime. Boot CPU only,
 * interrupts disabled, with no locks, allocation, logging or hardware access. */
enum kernel_heap_result kernel_heap_initialize(struct kernel_heap *heap,
    void *arena, size_t arena_bytes, struct kernel_heap_block *blocks,
    size_t block_capacity)
{
    if (!kernel_heap_storage_valid(heap, arena, arena_bytes, blocks,
                                   block_capacity)) {
        return KERNEL_HEAP_INVALID;
    }
    if (!kernel_heap_descriptor_zeroed(heap)) {
        return KERNEL_HEAP_INVALID;
    }

    /* All failure checks are complete. Clearing every record prevents stale
     * caller data from becoming a future list link when an inactive slot splits.
     * Explicit field stores also prevent a freestanding compiler from lowering
     * an aggregate-clear loop into an unavailable hosted memset() call. */
    for (size_t index = 0; index < block_capacity; ++index) {
        blocks[index].offset_bytes = 0;
        blocks[index].length_bytes = 0;
        blocks[index].previous_index = SIZE_MAX;
        blocks[index].next_index = SIZE_MAX;
        blocks[index].owner = KERNEL_HEAP_OWNER_FREE;
        blocks[index].slot_active = false;
        blocks[index].allocated = false;
    }
    blocks[0].length_bytes = arena_bytes;
    blocks[0].slot_active = true;
    /* The descriptor was proven completely zeroed above. Assign only its
     * nonzero/pointer fields so no aggregate copy needs a hosted runtime helper. */
    heap->arena = arena;
    heap->arena_bytes = arena_bytes;
    heap->blocks = blocks;
    heap->block_capacity = block_capacity;
    heap->active_blocks = 1;
    heap->free_bytes = arena_bytes;
    heap->initialized = true;
    return KERNEL_HEAP_OK;
}

/* Return whether one token names a live heap allocation group. Pure and with
 * no state access; FREE is metadata state and is never an allocation owner. */
static bool kernel_heap_owner_valid(enum kernel_heap_owner owner)
{
    return owner >= KERNEL_HEAP_OWNER_KERNEL &&
        owner < KERNEL_HEAP_OWNER_COUNT;
}

/* Allocate the first sufficiently large free block. Every fallible check,
 * including split-slot discovery and counter bounds, precedes payload clearing.
 * The rounded payload is cleared before metadata/output publication. All state
 * remains caller-owned; boot CPU only, interrupts disabled, no locks or IRQ use. */
enum kernel_heap_result kernel_heap_allocate(struct kernel_heap *heap,
    enum kernel_heap_owner owner, size_t size_bytes, void **allocation)
{
    if (heap == NULL || allocation == NULL) {
        return KERNEL_HEAP_INVALID;
    }
    if (!kernel_heap_owner_valid(owner)) {
        return KERNEL_HEAP_BAD_OWNER;
    }
    if (size_bytes == 0 || size_bytes > KERNEL_HEAP_MAX_ALLOCATION_BYTES ||
        size_bytes > SIZE_MAX - (KERNEL_HEAP_ALIGNMENT_BYTES - 1)) {
        return KERNEL_HEAP_BAD_SIZE;
    }
    if (!kernel_heap_check(heap)) {
        return KERNEL_HEAP_CORRUPT;
    }
    if (!kernel_heap_output_disjoint(heap, allocation)) {
        return KERNEL_HEAP_INVALID;
    }
    size_t rounded_bytes =
        (size_bytes + (KERNEL_HEAP_ALIGNMENT_BYTES - 1)) &
        ~(size_t)(KERNEL_HEAP_ALIGNMENT_BYTES - 1);

    size_t selected_index = SIZE_MAX;
    size_t split_index = SIZE_MAX;
    size_t current_index = heap->first_block_index;
    for (size_t visited = 0; visited < heap->active_blocks; ++visited) {
        const struct kernel_heap_block *block = &heap->blocks[current_index];
        if (!block->allocated && block->length_bytes >= rounded_bytes) {
            selected_index = current_index;
            if (block->length_bytes > rounded_bytes) {
                for (size_t index = 0; index < heap->block_capacity; ++index) {
                    if (!heap->blocks[index].slot_active) {
                        split_index = index;
                        break;
                    }
                }
                if (split_index == SIZE_MAX) {
                    return KERNEL_HEAP_METADATA_EXHAUSTED;
                }
            }
            break;
        }
        current_index = block->next_index;
    }
    if (selected_index == SIZE_MAX) {
        return KERNEL_HEAP_OOM;
    }

    const struct kernel_heap_block *selected = &heap->blocks[selected_index];
    if (heap->allocated_bytes > SIZE_MAX - rounded_bytes ||
        heap->free_bytes < rounded_bytes || heap->live_allocations == SIZE_MAX ||
        heap->owner_bytes[owner] > SIZE_MAX - rounded_bytes ||
        heap->owner_allocations[owner] == SIZE_MAX ||
        (split_index != SIZE_MAX && heap->active_blocks == SIZE_MAX)) {
        return KERNEL_HEAP_CORRUPT;
    }
    size_t selected_offset = selected->offset_bytes;
    size_t selected_length = selected->length_bytes;
    size_t selected_next = selected->next_index;

    /* Volatile is unnecessary: these are ordinary owned bytes, and the later
     * published pointer makes the stores observable under the C abstract model. */
    for (size_t index = 0; index < rounded_bytes; ++index) {
        heap->arena[selected_offset + index] = 0;
    }

    if (split_index != SIZE_MAX) {
        struct kernel_heap_block remainder = {
            .offset_bytes = selected_offset + rounded_bytes,
            .length_bytes = selected_length - rounded_bytes,
            .previous_index = selected_index,
            .next_index = selected_next,
            .owner = KERNEL_HEAP_OWNER_FREE,
            .slot_active = true,
            .allocated = false
        };
        heap->blocks[split_index] = remainder;
        if (selected_next != SIZE_MAX) {
            heap->blocks[selected_next].previous_index = split_index;
        }
        heap->blocks[selected_index].length_bytes = rounded_bytes;
        heap->blocks[selected_index].next_index = split_index;
        ++heap->active_blocks;
    }
    heap->blocks[selected_index].owner = owner;
    heap->blocks[selected_index].allocated = true;
    heap->allocated_bytes += rounded_bytes;
    heap->free_bytes -= rounded_bytes;
    ++heap->live_allocations;
    heap->owner_bytes[owner] += rounded_bytes;
    ++heap->owner_allocations[owner];

    *allocation = &heap->arena[selected_offset];
    return KERNEL_HEAP_OK;
}
