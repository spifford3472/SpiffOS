/* Purpose: adversarially compare the M4-M5 heap with an independent model.
 * Responsibilities: run deterministic mixed-owner allocation, release and
 * cleanup sequences; verify first fit, OOM atomicity, complete coalescence,
 * zero-on-reuse, guards and independently recomputed owner accounting.
 * Dependencies: the production kernel-heap API and ordinary guarded host RAM.
 * Invariants: the model uses one allocation identity per 16-byte arena unit and
 * never reads production records to decide expected behavior or counters.
 * Reference: docs/decisions/ADR-memory-services.md, M4-M5 acceptance contract. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel-heap.h"

enum {
    MODEL_ARENA_BYTES = 4096,
    MODEL_UNIT_BYTES = KERNEL_HEAP_ALIGNMENT_BYTES,
    MODEL_UNITS = MODEL_ARENA_BYTES / MODEL_UNIT_BYTES,
    MODEL_BLOCK_CAPACITY = MODEL_UNITS,
    MODEL_GUARD_BYTES = 64,
    MODEL_STEPS = 4000
};

struct guarded_model_arena {
    uint8_t before[MODEL_GUARD_BYTES];
    _Alignas(KERNEL_HEAP_ALIGNMENT_BYTES) uint8_t bytes[MODEL_ARENA_BYTES];
    uint8_t after[MODEL_GUARD_BYTES];
};

struct model_allocation {
    bool live;
    size_t offset_units;
    size_t length_units;
    enum kernel_heap_owner owner;
    uint8_t payload_pattern;
    void *production_pointer;
};

struct heap_model {
    /* Zero means free. A nonzero value is allocation-array index plus one.
     * Keeping allocation identities, rather than only owners, lets the model
     * distinguish adjacent allocations belonging to the same owner. */
    uint16_t unit_allocation[MODEL_UNITS];
    struct model_allocation allocations[MODEL_UNITS];
};

/* Return a reproducible pseudo-random value for sequence selection. The fixed
 * state is test input, not entropy or a security primitive. Only *state changes. */
static uint32_t deterministic_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

/* Check both fixed guards without consulting sanitizer metadata. The arena is
 * borrowed read-only and the expected pattern is supplied in bytes. */
static void assert_model_guards(const struct guarded_model_arena *storage,
    uint8_t expected_pattern)
{
    for (size_t index = 0; index < MODEL_GUARD_BYTES; ++index) {
        assert(storage->before[index] == expected_pattern);
        assert(storage->after[index] == expected_pattern);
    }
}

/* Find the first free run that can satisfy length_units, exactly modeling the
 * documented address-ordered first-fit policy. Return false for model OOM and
 * leave *offset_units unchanged. Model storage is borrowed read-only. */
static bool model_first_fit(const struct heap_model *model,
    size_t length_units, size_t *offset_units)
{
    for (size_t start = 0; start + length_units <= MODEL_UNITS; ++start) {
        size_t present = 0;
        while (present < length_units &&
               model->unit_allocation[start + present] == 0) {
            ++present;
        }
        if (present == length_units) {
            *offset_units = start;
            return true;
        }
        start += present;
    }
    return false;
}

/* Return an inactive allocation identity for a successful model allocation.
 * At most one live allocation can begin in each arena unit, so MODEL_UNITS
 * entries are sufficient. This helper changes nothing. */
static size_t model_free_allocation_index(const struct heap_model *model)
{
    for (size_t index = 0; index < MODEL_UNITS; ++index) {
        if (!model->allocations[index].live) {
            return index;
        }
    }
    return SIZE_MAX;
}

/* Return the allocation-array index of the ordinal-th live allocation. The
 * model is borrowed read-only; callers obtain the live count first. */
static size_t model_live_allocation_index(const struct heap_model *model,
    size_t ordinal)
{
    for (size_t index = 0; index < MODEL_UNITS; ++index) {
        if (model->allocations[index].live) {
            if (ordinal == 0) {
                return index;
            }
            --ordinal;
        }
    }
    return SIZE_MAX;
}

/* Recompute the entire expected graph and accounting without calling the
 * production invariant checker. Production descriptor, records and payload are
 * borrowed read-only. Live payload bytes must retain their model pattern. */
static void assert_heap_matches_model(const struct kernel_heap *heap,
    const struct heap_model *model, const struct guarded_model_arena *storage,
    uint8_t guard_pattern)
{
    size_t expected_owner_bytes[KERNEL_HEAP_OWNER_COUNT] = {0};
    size_t expected_owner_allocations[KERNEL_HEAP_OWNER_COUNT] = {0};
    size_t expected_allocated_bytes = 0;
    size_t expected_live_allocations = 0;

    for (size_t index = 0; index < MODEL_UNITS; ++index) {
        const struct model_allocation *allocation = &model->allocations[index];
        if (!allocation->live) {
            continue;
        }
        size_t bytes = allocation->length_units * MODEL_UNIT_BYTES;
        expected_owner_bytes[allocation->owner] += bytes;
        ++expected_owner_allocations[allocation->owner];
        expected_allocated_bytes += bytes;
        ++expected_live_allocations;
        for (size_t byte = 0; byte < bytes; ++byte) {
            size_t arena_offset = allocation->offset_units * MODEL_UNIT_BYTES;
            assert(storage->bytes[arena_offset + byte] ==
                   allocation->payload_pattern);
        }
    }

    assert(heap->allocated_bytes == expected_allocated_bytes);
    assert(heap->free_bytes == MODEL_ARENA_BYTES - expected_allocated_bytes);
    assert(heap->live_allocations == expected_live_allocations);
    for (size_t owner = 0; owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        assert(heap->owner_bytes[owner] == expected_owner_bytes[owner]);
        assert(heap->owner_allocations[owner] ==
               expected_owner_allocations[owner]);
    }

    size_t expected_offset_units = 0;
    size_t production_index = heap->first_block_index;
    size_t expected_blocks = 0;
    while (expected_offset_units < MODEL_UNITS) {
        uint16_t identity = model->unit_allocation[expected_offset_units];
        size_t run_units = 1;
        while (expected_offset_units + run_units < MODEL_UNITS &&
               model->unit_allocation[expected_offset_units + run_units] ==
                   identity) {
            ++run_units;
        }

        assert(expected_blocks < heap->active_blocks);
        assert(production_index < heap->block_capacity);
        const struct kernel_heap_block *block = &heap->blocks[production_index];
        assert(block->slot_active);
        assert(block->offset_bytes == expected_offset_units * MODEL_UNIT_BYTES);
        assert(block->length_bytes == run_units * MODEL_UNIT_BYTES);
        if (identity == 0) {
            assert(!block->allocated);
            assert(block->owner == KERNEL_HEAP_OWNER_FREE);
        } else {
            const struct model_allocation *allocation =
                &model->allocations[identity - 1];
            assert(allocation->live);
            assert(block->allocated);
            assert(block->owner == allocation->owner);
        }
        production_index = block->next_index;
        expected_offset_units += run_units;
        ++expected_blocks;
    }
    assert(expected_blocks == heap->active_blocks);
    assert(production_index == SIZE_MAX);
    assert(kernel_heap_check(heap));
    assert_model_guards(storage, guard_pattern);
}

/* Snapshot every production-owned byte and prove an invalid or OOM allocation
 * is atomic. The model is not involved because these requests must not change
 * it. All fixture storage remains borrowed by the caller. */
static void assert_allocation_failure_atomic(struct kernel_heap *heap,
    struct kernel_heap_block *blocks, struct guarded_model_arena *storage,
    enum kernel_heap_owner owner, size_t request_bytes,
    enum kernel_heap_result expected_result)
{
    struct kernel_heap saved_heap;
    struct kernel_heap_block saved_blocks[MODEL_BLOCK_CAPACITY];
    struct guarded_model_arena saved_storage;
    memcpy(&saved_heap, heap, sizeof saved_heap);
    memcpy(saved_blocks, blocks, sizeof saved_blocks);
    memcpy(&saved_storage, storage, sizeof saved_storage);
    void *output = (void *)(uintptr_t)UINT64_C(0x12345678);
    assert(kernel_heap_allocate(heap, owner, request_bytes, &output) ==
           expected_result);
    assert(output == (void *)(uintptr_t)UINT64_C(0x12345678));
    assert(memcmp(&saved_heap, heap, sizeof saved_heap) == 0);
    assert(memcmp(saved_blocks, blocks, sizeof saved_blocks) == 0);
    assert(memcmp(&saved_storage, storage, sizeof saved_storage) == 0);
}

/* Allocate through production only after independently finding the expected
 * first fit. On success, verify all rounded bytes were cleared before filling
 * them with a nonzero survivor pattern and publishing the allocation to model
 * state. OOM is checked byte-for-byte atomic. */
static bool model_allocate(struct kernel_heap *heap,
    struct kernel_heap_block *blocks, struct guarded_model_arena *storage,
    struct heap_model *model, enum kernel_heap_owner owner,
    size_t request_bytes, size_t *oom_count)
{
    size_t rounded_units =
        (request_bytes + MODEL_UNIT_BYTES - 1) / MODEL_UNIT_BYTES;
    size_t expected_offset_units = SIZE_MAX;
    if (!model_first_fit(model, rounded_units, &expected_offset_units)) {
        assert_allocation_failure_atomic(heap, blocks, storage, owner,
                                         request_bytes, KERNEL_HEAP_OOM);
        ++*oom_count;
        return false;
    }

    size_t allocation_index = model_free_allocation_index(model);
    assert(allocation_index != SIZE_MAX);
    void *pointer = (void *)(uintptr_t)UINT64_C(0x87654321);
    assert(kernel_heap_allocate(heap, owner, request_bytes, &pointer) ==
           KERNEL_HEAP_OK);
    assert(pointer == &storage->bytes[expected_offset_units * MODEL_UNIT_BYTES]);
    size_t rounded_bytes = rounded_units * MODEL_UNIT_BYTES;
    for (size_t byte = 0; byte < rounded_bytes; ++byte) {
        assert(((uint8_t *)pointer)[byte] == 0);
    }

    uint8_t pattern = (uint8_t)(0x40u + (allocation_index % 0x3fu));
    memset(pointer, pattern, rounded_bytes);
    model->allocations[allocation_index] = (struct model_allocation) {
        .live = true,
        .offset_units = expected_offset_units,
        .length_units = rounded_units,
        .owner = owner,
        .payload_pattern = pattern,
        .production_pointer = pointer
    };
    for (size_t unit = 0; unit < rounded_units; ++unit) {
        assert(model->unit_allocation[expected_offset_units + unit] == 0);
        model->unit_allocation[expected_offset_units + unit] =
            (uint16_t)(allocation_index + 1);
    }
    return true;
}

/* Release one live identity through production, then free exactly its modeled
 * units. Production retains payload; later allocation checks zero-on-reuse. */
static void model_release(struct kernel_heap *heap, struct heap_model *model,
    size_t allocation_index)
{
    struct model_allocation *allocation = &model->allocations[allocation_index];
    assert(allocation->live);
    assert(kernel_heap_release(heap, allocation->owner,
                               allocation->production_pointer) ==
           KERNEL_HEAP_OK);
    for (size_t unit = 0; unit < allocation->length_units; ++unit) {
        size_t arena_unit = allocation->offset_units + unit;
        assert(model->unit_allocation[arena_unit] == allocation_index + 1);
        model->unit_allocation[arena_unit] = 0;
    }
    allocation->live = false;
}

/* Remove every modeled allocation for owner after production owner cleanup.
 * The fixed API choice keeps session and workspace lifetimes separate. */
static void model_cleanup_owner(struct kernel_heap *heap,
    struct heap_model *model, enum kernel_heap_owner owner)
{
    if (owner == KERNEL_HEAP_OWNER_SESSION) {
        assert(kernel_heap_release_session(heap) == KERNEL_HEAP_OK);
    } else {
        assert(owner >= KERNEL_HEAP_OWNER_FIRST_WORKSPACE &&
               owner <= KERNEL_HEAP_OWNER_LAST_WORKSPACE);
        assert(kernel_heap_release_workspace(heap, owner) == KERNEL_HEAP_OK);
    }
    for (size_t index = 0; index < MODEL_UNITS; ++index) {
        struct model_allocation *allocation = &model->allocations[index];
        if (!allocation->live || allocation->owner != owner) {
            continue;
        }
        for (size_t unit = 0; unit < allocation->length_units; ++unit) {
            size_t arena_unit = allocation->offset_units + unit;
            assert(model->unit_allocation[arena_unit] == index + 1);
            model->unit_allocation[arena_unit] = 0;
        }
        allocation->live = false;
    }
}

/* Exercise a long deterministic mix against the independent unit model. The
 * sequence deliberately includes pressure/OOM, every owner, repeated workspace
 * cleanup, separate session cleanup, fragmentation and dirty reuse. */
static void test_deterministic_model_sequence(void)
{
    const uint8_t guard_pattern = 0xa7;
    struct guarded_model_arena storage;
    memset(&storage, guard_pattern, sizeof storage);
    struct kernel_heap_block blocks[MODEL_BLOCK_CAPACITY];
    memset(blocks, 0x5c, sizeof blocks);
    struct kernel_heap heap = {0};
    struct heap_model model = {0};
    assert(kernel_heap_initialize(&heap, storage.bytes, sizeof storage.bytes,
                                  blocks, MODEL_BLOCK_CAPACITY) ==
           KERNEL_HEAP_OK);
    assert_heap_matches_model(&heap, &model, &storage, guard_pattern);

    /* These cover zero, explicit maximum overflow and checked round-up overflow
     * paths before the mixed sequence. Every byte must remain unchanged. */
    assert_allocation_failure_atomic(&heap, blocks, &storage,
        KERNEL_HEAP_OWNER_KERNEL, 0, KERNEL_HEAP_BAD_SIZE);
    assert_allocation_failure_atomic(&heap, blocks, &storage,
        KERNEL_HEAP_OWNER_KERNEL, KERNEL_HEAP_MAX_ALLOCATION_BYTES + 1,
        KERNEL_HEAP_BAD_SIZE);
    assert_allocation_failure_atomic(&heap, blocks, &storage,
        KERNEL_HEAP_OWNER_KERNEL, SIZE_MAX, KERNEL_HEAP_BAD_SIZE);

    uint32_t random_state = UINT32_C(0xc0ffee29);
    size_t oom_count = 0;
    size_t successful_allocations = 0;
    size_t workspace_cleanups = 0;
    size_t session_cleanups = 0;
    for (size_t step = 0; step < MODEL_STEPS; ++step) {
        uint32_t choice = deterministic_next(&random_state);
        size_t live_count = heap.live_allocations;
        if ((choice % 100) < 58 || live_count == 0) {
            enum kernel_heap_owner owner = (enum kernel_heap_owner)
                (KERNEL_HEAP_OWNER_KERNEL +
                 deterministic_next(&random_state) %
                     (KERNEL_HEAP_OWNER_COUNT - KERNEL_HEAP_OWNER_KERNEL));
            /* Most requests are small enough to fragment realistically; every
             * 31st allocation asks for more than the arena to force OOM. */
            size_t request_bytes =
                1 + deterministic_next(&random_state) % 383;
            if (step % 31 == 0) {
                request_bytes = MODEL_ARENA_BYTES + 1;
            }
            if (model_allocate(&heap, blocks, &storage, &model, owner,
                               request_bytes, &oom_count)) {
                ++successful_allocations;
            }
        } else if ((choice % 100) < 78) {
            size_t ordinal = deterministic_next(&random_state) % live_count;
            size_t allocation_index =
                model_live_allocation_index(&model, ordinal);
            assert(allocation_index != SIZE_MAX);
            model_release(&heap, &model, allocation_index);
        } else if ((choice % 100) < 95) {
            enum kernel_heap_owner owner = (enum kernel_heap_owner)
                (KERNEL_HEAP_OWNER_FIRST_WORKSPACE +
                 deterministic_next(&random_state) %
                     (KERNEL_HEAP_OWNER_LAST_WORKSPACE -
                      KERNEL_HEAP_OWNER_FIRST_WORKSPACE + 1));
            model_cleanup_owner(&heap, &model, owner);
            /* Repeating selected cleanup calls proves empty cleanup remains
             * idempotent after arbitrary prior fragmentation. */
            if (step % 7 == 0) {
                model_cleanup_owner(&heap, &model, owner);
            }
            ++workspace_cleanups;
        } else {
            model_cleanup_owner(&heap, &model, KERNEL_HEAP_OWNER_SESSION);
            ++session_cleanups;
        }
        assert_heap_matches_model(&heap, &model, &storage, guard_pattern);
    }

    assert(successful_allocations > 500);
    assert(oom_count > 50);
    assert(workspace_cleanups > 100);
    assert(session_cleanups > 20);

    /* Retire every non-kernel lifetime through its scoped API, then release
     * remaining kernel allocations individually. The independent model must
     * reduce to one free run and production must expose one coalesced record. */
    model_cleanup_owner(&heap, &model, KERNEL_HEAP_OWNER_SESSION);
    for (enum kernel_heap_owner owner = KERNEL_HEAP_OWNER_FIRST_WORKSPACE;
         owner <= KERNEL_HEAP_OWNER_LAST_WORKSPACE;
         owner = (enum kernel_heap_owner)(owner + 1)) {
        model_cleanup_owner(&heap, &model, owner);
        model_cleanup_owner(&heap, &model, owner);
    }
    for (size_t index = 0; index < MODEL_UNITS; ++index) {
        if (model.allocations[index].live) {
            assert(model.allocations[index].owner == KERNEL_HEAP_OWNER_KERNEL);
            model_release(&heap, &model, index);
        }
    }
    assert_heap_matches_model(&heap, &model, &storage, guard_pattern);
    assert(heap.active_blocks == 1);
    const struct kernel_heap_block *only =
        &heap.blocks[heap.first_block_index];
    assert(only->offset_bytes == 0 && only->length_bytes == MODEL_ARENA_BYTES);
    assert(!only->allocated && only->owner == KERNEL_HEAP_OWNER_FREE);
    assert(only->previous_index == SIZE_MAX && only->next_index == SIZE_MAX);
}

/* Assertion or sanitizer failure terminates with nonzero status. */
int main(void)
{
    test_deterministic_model_sequence();
    puts("PASS kernel heap model: deterministic owners, OOM, cleanup and reuse");
}
