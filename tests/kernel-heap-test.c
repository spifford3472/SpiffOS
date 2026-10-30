/* Purpose: verify bounded M4 allocation, checked release and failure atomicity.
 * Responsibilities: exercise owned ordinary-page backing, alignment, splitting,
 * complete zero-before-publish, invalid-free rejection, fragmentation,
 * two-sided coalescing, reuse, capacity failures and metadata corruption.
 * Dependencies: production M1/M2/M3 policy and M4 heap over guarded host RAM.
 * Invariants: numeric physical/direct-map values are never host pointers; only
 * the injected backend translates its one expected page into ordinary test RAM.
 * Reference: docs/decisions/ADR-memory-services.md, M3 and M4 contracts. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel-heap.h"
#include "page-access.h"

enum {
    TEST_PAGE_BYTES = 4096,
    TEST_GUARD_BYTES = 32,
    TEST_BLOCK_CAPACITY = 16
};

struct guarded_arena {
    uint8_t before[TEST_GUARD_BYTES];
    _Alignas(KERNEL_HEAP_ALIGNMENT_BYTES) uint8_t bytes[TEST_PAGE_BYTES];
    uint8_t after[TEST_GUARD_BYTES];
};

struct zero_page_context {
    struct guarded_arena *storage;
    uint64_t expected_mapped_address;
    size_t calls;
};

/* Translate only the single numeric M3 address established by this fixture and
 * clear exactly one page. Context and guarded RAM remain caller-owned; success
 * is synchronous, performs no allocation and leaves both guards untouched. */
static bool zero_owned_test_page(void *opaque, uint64_t mapped_address,
    size_t length_bytes)
{
    struct zero_page_context *context = opaque;
    if (context == NULL || context->storage == NULL ||
        mapped_address != context->expected_mapped_address ||
        length_bytes != TEST_PAGE_BYTES) {
        return false;
    }
    ++context->calls;
    memset(context->storage->bytes, 0, length_bytes);
    return true;
}

/* Check fixed guard patterns independently of ASan so a bounded but incorrect
 * payload clear also fails deterministically. Borrow storage read-only. */
static void assert_guards(const struct guarded_arena *storage, uint8_t pattern)
{
    for (size_t index = 0; index < TEST_GUARD_BYTES; ++index) {
        assert(storage->before[index] == pattern);
        assert(storage->after[index] == pattern);
    }
}

/* Prove backing comes from an M3-authorized kernel-owned ordinary page. The M2
 * reserve counter must remain unchanged because page_access_allocate_zero()
 * always requests the ordinary pool. The returned physical address is numeric
 * only; the test backend alone maps it to guarded host RAM. */
static void prepare_owned_backing(struct guarded_arena *storage,
    struct frame_allocator *allocator, uint16_t *owner_slots,
    uint64_t *physical_address)
{
    static const struct memory_region regions[] = {
        {UINT64_C(0x1000), 3 * TEST_PAGE_BYTES, MEMORY_REGION_USABLE}
    };
    memset(storage, 0x5a, sizeof *storage);
    *allocator = (struct frame_allocator){0};
    assert(frame_allocator_initialize(allocator, regions, 1, owner_slots, 3,
                                      1) == FRAME_OK);
    struct page_access_policy policy = {
        .allocator = allocator,
        .direct_map_offset = UINT64_C(0x100000),
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    struct zero_page_context zero_context = {
        .storage = storage,
        .expected_mapped_address = UINT64_C(0x102000)
    };
    uint64_t published = UINT64_C(0xfeedface);
    assert(page_access_allocate_zero(allocator, &policy, FRAME_OWNER_KERNEL,
                                     zero_owned_test_page, &zero_context,
                                     &published) == PAGE_ACCESS_OK);
    assert(published == UINT64_C(0x2000));
    assert(zero_context.calls == 1);
    assert(allocator->reserve_free == 1);
    assert(allocator->ordinary_free == 1);
    assert(allocator->owned[FRAME_OWNER_KERNEL] == 1);
    assert(frame_allocator_check(allocator));
    for (size_t index = 0; index < TEST_PAGE_BYTES; ++index) {
        assert(storage->bytes[index] == 0);
    }
    assert_guards(storage, 0x5a);
    *physical_address = published;
}

/* Validate first-fit splits, rounded accounting and clearing of every rounded
 * byte before pointer publication. Dirty bytes outside each selected block and
 * both guards must remain untouched. */
static void test_owned_page_initialization_and_allocation(void)
{
    struct guarded_arena storage;
    struct frame_allocator allocator;
    uint16_t owner_slots[3];
    uint64_t physical_address;
    prepare_owned_backing(&storage, &allocator, owner_slots, &physical_address);

    struct kernel_heap heap = {0};
    struct kernel_heap_block blocks[TEST_BLOCK_CAPACITY];
    memset(blocks, 0xa5, sizeof blocks);
    assert(kernel_heap_initialize(&heap, storage.bytes, sizeof storage.bytes,
                                  blocks, TEST_BLOCK_CAPACITY) == KERNEL_HEAP_OK);
    assert(kernel_heap_check(&heap));
    assert(heap.active_blocks == 1 && heap.free_bytes == TEST_PAGE_BYTES);

    /* Simulate dirty kernel-only reuse after the backing page was initially
     * zeroed. M4 must still clear each allocation before it publishes a pointer. */
    memset(storage.bytes, 0xcc, sizeof storage.bytes);
    void *first = (void *)(uintptr_t)UINT64_C(0x11111111);
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_KERNEL, 1, &first) ==
           KERNEL_HEAP_OK);
    assert(first == storage.bytes);
    for (size_t index = 0; index < 16; ++index) {
        assert(storage.bytes[index] == 0);
    }
    assert(storage.bytes[16] == 0xcc);

    void *second = NULL;
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_SESSION, 17, &second) ==
           KERNEL_HEAP_OK);
    assert(second == &storage.bytes[16]);
    assert((uintptr_t)second % KERNEL_HEAP_ALIGNMENT_BYTES == 0);
    for (size_t index = 16; index < 48; ++index) {
        assert(storage.bytes[index] == 0);
    }
    assert(storage.bytes[48] == 0xcc);
    assert(heap.allocated_bytes == 48);
    assert(heap.free_bytes == TEST_PAGE_BYTES - 48);
    assert(heap.live_allocations == 2);
    assert(heap.owner_bytes[KERNEL_HEAP_OWNER_KERNEL] == 16);
    assert(heap.owner_bytes[KERNEL_HEAP_OWNER_SESSION] == 32);
    assert(heap.owner_allocations[KERNEL_HEAP_OWNER_KERNEL] == 1);
    assert(heap.owner_allocations[KERNEL_HEAP_OWNER_SESSION] == 1);
    assert(kernel_heap_check(&heap));
    assert_guards(&storage, 0x5a);

    /* End the borrowed heap lifetime before returning its backing frame. There
     * is no M4 teardown API in this allocation-only checkpoint. */
    heap = (struct kernel_heap){0};
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, physical_address) ==
           FRAME_OK);
    assert(allocator.reserve_free == 1 && allocator.ordinary_free == 2);
    assert(frame_allocator_check(&allocator));
}

/* Compare all mutable heap storage after a rejected allocation. This directly
 * checks the contract beyond relying on production kernel_heap_check(). */
static void assert_failed_allocation_unchanged(struct kernel_heap *heap,
    struct kernel_heap_block *blocks, size_t block_capacity,
    uint8_t *arena, size_t arena_bytes, enum kernel_heap_owner owner,
    size_t request_bytes, enum kernel_heap_result expected)
{
    struct kernel_heap saved_heap;
    struct kernel_heap_block saved_blocks[TEST_BLOCK_CAPACITY];
    uint8_t saved_arena[TEST_PAGE_BYTES];
    assert(block_capacity <= TEST_BLOCK_CAPACITY);
    assert(arena_bytes <= TEST_PAGE_BYTES);
    memcpy(&saved_heap, heap, sizeof saved_heap);
    memcpy(saved_blocks, blocks, block_capacity * sizeof *blocks);
    memcpy(saved_arena, arena, arena_bytes);
    void *output = (void *)(uintptr_t)UINT64_C(0xabcdef01);
    assert(kernel_heap_allocate(heap, owner, request_bytes, &output) == expected);
    assert(output == (void *)(uintptr_t)UINT64_C(0xabcdef01));
    assert(memcmp(&saved_heap, heap, sizeof saved_heap) == 0);
    assert(memcmp(saved_blocks, blocks,
                  block_capacity * sizeof *blocks) == 0);
    assert(memcmp(saved_arena, arena, arena_bytes) == 0);
}

/* Compare descriptor, every record and payload after a rejected release. The
 * candidate pointer is passed through unchanged and production must not read it;
 * this helper borrows all fixture storage and performs no lasting mutation. */
static void assert_failed_release_unchanged(struct kernel_heap *heap,
    struct kernel_heap_block *blocks, size_t block_capacity,
    uint8_t *arena, size_t arena_bytes, enum kernel_heap_owner owner,
    void *allocation, enum kernel_heap_result expected)
{
    struct kernel_heap saved_heap;
    struct kernel_heap_block saved_blocks[TEST_BLOCK_CAPACITY];
    uint8_t saved_arena[TEST_PAGE_BYTES];
    assert(block_capacity <= TEST_BLOCK_CAPACITY);
    assert(arena_bytes <= TEST_PAGE_BYTES);
    memcpy(&saved_heap, heap, sizeof saved_heap);
    memcpy(saved_blocks, blocks, block_capacity * sizeof *blocks);
    memcpy(saved_arena, arena, arena_bytes);
    assert(kernel_heap_release(heap, owner, allocation) == expected);
    assert(memcmp(&saved_heap, heap, sizeof saved_heap) == 0);
    assert(memcmp(saved_blocks, blocks,
                  block_capacity * sizeof *blocks) == 0);
    assert(memcmp(saved_arena, arena, arena_bytes) == 0);
}

/* Exercise distinct input/OOM/capacity results and prove each preserves output,
 * descriptor, records and payload. Metadata exhaustion must not silently hand
 * the caller an oversized block. */
static void test_allocation_failures_are_atomic(void)
{
    _Alignas(KERNEL_HEAP_ALIGNMENT_BYTES) uint8_t arena[64];
    struct kernel_heap_block blocks[4];
    struct kernel_heap heap = {0};
    memset(arena, 0x7b, sizeof arena);
    memset(blocks, 0x3c, sizeof blocks);
    assert(kernel_heap_initialize(&heap, arena, sizeof arena, blocks, 4) ==
           KERNEL_HEAP_OK);
    assert_failed_allocation_unchanged(&heap, blocks, 4, arena, sizeof arena,
        KERNEL_HEAP_OWNER_FREE, 16, KERNEL_HEAP_BAD_OWNER);
    assert_failed_allocation_unchanged(&heap, blocks, 4, arena, sizeof arena,
        KERNEL_HEAP_OWNER_KERNEL, 0, KERNEL_HEAP_BAD_SIZE);
    assert_failed_allocation_unchanged(&heap, blocks, 4, arena, sizeof arena,
        KERNEL_HEAP_OWNER_KERNEL, SIZE_MAX, KERNEL_HEAP_BAD_SIZE);
    assert_failed_allocation_unchanged(&heap, blocks, 4, arena, sizeof arena,
        KERNEL_HEAP_OWNER_KERNEL, KERNEL_HEAP_MAX_ALLOCATION_BYTES + 1,
        KERNEL_HEAP_BAD_SIZE);
    assert_failed_allocation_unchanged(&heap, blocks, 4, arena, sizeof arena,
        KERNEL_HEAP_OWNER_KERNEL, 80, KERNEL_HEAP_OOM);

    struct kernel_heap saved_heap;
    struct kernel_heap_block saved_blocks[4];
    uint8_t saved_arena[64];
    memcpy(&saved_heap, &heap, sizeof heap);
    memcpy(saved_blocks, blocks, sizeof blocks);
    memcpy(saved_arena, arena, sizeof arena);
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_KERNEL, 16,
        (void **)(void *)arena) == KERNEL_HEAP_INVALID);
    assert(memcmp(&saved_heap, &heap, sizeof heap) == 0);
    assert(memcmp(saved_blocks, blocks, sizeof blocks) == 0);
    assert(memcmp(saved_arena, arena, sizeof arena) == 0);

    struct kernel_heap one_record_heap = {0};
    struct kernel_heap_block one_record[1];
    _Alignas(KERNEL_HEAP_ALIGNMENT_BYTES) uint8_t one_record_arena[64];
    memset(one_record_arena, 0x6d, sizeof one_record_arena);
    assert(kernel_heap_initialize(&one_record_heap, one_record_arena,
        sizeof one_record_arena, one_record, 1) == KERNEL_HEAP_OK);
    assert_failed_allocation_unchanged(&one_record_heap, one_record, 1,
        one_record_arena, sizeof one_record_arena, KERNEL_HEAP_OWNER_KERNEL,
        16, KERNEL_HEAP_METADATA_EXHAUSTED);

    void *whole = NULL;
    assert(kernel_heap_allocate(&one_record_heap,
                                KERNEL_HEAP_OWNER_FIRST_WORKSPACE,
                                sizeof one_record_arena, &whole) ==
           KERNEL_HEAP_OK);
    assert(whole == one_record_arena);
    for (size_t index = 0; index < sizeof one_record_arena; ++index) {
        assert(one_record_arena[index] == 0);
    }
    assert(kernel_heap_check(&one_record_heap));
    assert_failed_allocation_unchanged(&one_record_heap, one_record, 1,
        one_record_arena, sizeof one_record_arena, KERNEL_HEAP_OWNER_KERNEL,
        16, KERNEL_HEAP_OOM);
}

/* Cover checked initialization ranges and corrupt-list rejection. Failed
 * initialization preserves all caller bytes; corrupt allocation returns without
 * attempting repair or changing payload/output/counters. */
static void test_initialization_and_corruption_failures(void)
{
    _Alignas(KERNEL_HEAP_ALIGNMENT_BYTES) uint8_t arena[64];
    struct kernel_heap_block blocks[4];
    struct kernel_heap heap = {0};
    struct kernel_heap saved_heap;
    struct kernel_heap_block saved_blocks[4];
    uint8_t saved_arena[64];
    memset(arena, 0x42, sizeof arena);
    memset(blocks, 0x24, sizeof blocks);
    memcpy(&saved_heap, &heap, sizeof heap);
    memcpy(saved_blocks, blocks, sizeof blocks);
    memcpy(saved_arena, arena, sizeof arena);
    assert(kernel_heap_initialize(&heap, arena + 1, 48, blocks, 4) ==
           KERNEL_HEAP_INVALID);
    assert(memcmp(&saved_heap, &heap, sizeof heap) == 0);
    assert(memcmp(saved_blocks, blocks, sizeof blocks) == 0);
    assert(memcmp(saved_arena, arena, sizeof arena) == 0);
    assert(kernel_heap_initialize(&heap, arena, 63, blocks, 4) ==
           KERNEL_HEAP_INVALID);
    assert(kernel_heap_initialize(&heap, arena, sizeof arena,
        (struct kernel_heap_block *)(void *)arena, 1) == KERNEL_HEAP_INVALID);
    assert(memcmp(&saved_heap, &heap, sizeof heap) == 0);
    assert(memcmp(saved_blocks, blocks, sizeof blocks) == 0);
    assert(memcmp(saved_arena, arena, sizeof arena) == 0);

    heap.free_bytes = 1; /* A nonzero descriptor is not an unused descriptor. */
    assert(kernel_heap_initialize(&heap, arena, sizeof arena, blocks, 4) ==
           KERNEL_HEAP_INVALID);
    heap = (struct kernel_heap){0};
    assert(kernel_heap_initialize(&heap, arena, sizeof arena, blocks, 4) ==
           KERNEL_HEAP_OK);
    blocks[0].offset_bytes = 16;
    assert(!kernel_heap_check(&heap));
    assert_failed_allocation_unchanged(&heap, blocks, 4, arena, sizeof arena,
        KERNEL_HEAP_OWNER_KERNEL, 16, KERNEL_HEAP_CORRUPT);
}

/* Reject null, foreign, interior, wrong-owner and repeated frees before any
 * storage changes. The first block remains a current free-block boundary after
 * release, which makes its immediate second release an unambiguous double free. */
static void test_release_rejections_are_atomic(void)
{
    _Alignas(KERNEL_HEAP_ALIGNMENT_BYTES) uint8_t arena[128];
    struct kernel_heap_block blocks[TEST_BLOCK_CAPACITY];
    struct kernel_heap heap = {0};
    memset(arena, 0x6a, sizeof arena);
    assert(kernel_heap_initialize(&heap, arena, sizeof arena, blocks,
                                  TEST_BLOCK_CAPACITY) == KERNEL_HEAP_OK);
    void *first = NULL;
    void *second = NULL;
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_KERNEL, 32, &first) ==
           KERNEL_HEAP_OK);
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_SESSION, 16, &second) ==
           KERNEL_HEAP_OK);
    assert(first == arena && second == &arena[32]);

    uint8_t foreign_byte = 0;
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_KERNEL, NULL,
        KERNEL_HEAP_BAD_POINTER);
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_FREE, first,
        KERNEL_HEAP_BAD_OWNER);
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_KERNEL, &foreign_byte,
        KERNEL_HEAP_BAD_POINTER);
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_KERNEL, &arena[1],
        KERNEL_HEAP_BAD_POINTER);
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_KERNEL, &arena[16],
        KERNEL_HEAP_BAD_POINTER);
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_KERNEL, second,
        KERNEL_HEAP_WRONG_OWNER);

    memset(first, 0xd4, 32);
    assert(kernel_heap_release(&heap, KERNEL_HEAP_OWNER_KERNEL, first) ==
           KERNEL_HEAP_OK);
    for (size_t index = 0; index < 32; ++index) {
        assert(arena[index] == 0xd4); /* Free deliberately does not clear. */
    }
    assert(heap.allocated_bytes == 16 && heap.free_bytes == 112);
    assert(heap.live_allocations == 1);
    assert(heap.owner_bytes[KERNEL_HEAP_OWNER_KERNEL] == 0);
    assert(heap.owner_allocations[KERNEL_HEAP_OWNER_KERNEL] == 0);
    assert(kernel_heap_check(&heap));
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_KERNEL, first,
        KERNEL_HEAP_DOUBLE_FREE);

    blocks[heap.first_block_index].offset_bytes = 16;
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        arena, sizeof arena, KERNEL_HEAP_OWNER_SESSION, second,
        KERNEL_HEAP_CORRUPT);
}

/* Create two separated holes whose total space can satisfy a request but whose
 * largest member cannot. Releasing the intervening allocation must merge a free
 * predecessor and successor, after which first fit reuses the earliest address
 * and clears the complete rounded replacement block without touching guards. */
static void test_fragmentation_coalescing_and_reuse(void)
{
    struct guarded_arena storage;
    memset(&storage, 0x5c, sizeof storage);
    struct kernel_heap_block blocks[TEST_BLOCK_CAPACITY];
    struct kernel_heap heap = {0};
    assert(kernel_heap_initialize(&heap, storage.bytes, 128, blocks,
                                  TEST_BLOCK_CAPACITY) == KERNEL_HEAP_OK);

    void *first = NULL;
    void *left_hole = NULL;
    void *bridge = NULL;
    void *right_hole = NULL;
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_KERNEL, 16, &first) ==
           KERNEL_HEAP_OK);
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_SESSION, 32,
                                &left_hole) == KERNEL_HEAP_OK);
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_FIRST_WORKSPACE, 16,
                                &bridge) == KERNEL_HEAP_OK);
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_KERNEL, 32,
                                &right_hole) == KERNEL_HEAP_OK);
    assert(first == &storage.bytes[0] && left_hole == &storage.bytes[16]);
    assert(bridge == &storage.bytes[48] && right_hole == &storage.bytes[64]);
    memset(left_hole, 0xb1, 32);
    memset(bridge, 0xb2, 16);
    memset(right_hole, 0xb3, 32);

    assert(kernel_heap_release(&heap, KERNEL_HEAP_OWNER_SESSION, left_hole) ==
           KERNEL_HEAP_OK);
    assert(kernel_heap_release(&heap, KERNEL_HEAP_OWNER_KERNEL, right_hole) ==
           KERNEL_HEAP_OK);
    assert(heap.free_bytes == 96 && heap.active_blocks == 4);
    assert(kernel_heap_check(&heap));
    assert_failed_allocation_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        storage.bytes, 128, KERNEL_HEAP_OWNER_SESSION, 80, KERNEL_HEAP_OOM);

    assert(kernel_heap_release(&heap, KERNEL_HEAP_OWNER_FIRST_WORKSPACE,
                               bridge) == KERNEL_HEAP_OK);
    assert(heap.free_bytes == 112 && heap.active_blocks == 2);
    assert(heap.live_allocations == 1 && heap.allocated_bytes == 16);
    assert(kernel_heap_check(&heap));

    /* The old bridge boundary disappeared into the predecessor, so it is a
     * stale coalesced pointer rather than a current free-block double free. */
    assert_failed_release_unchanged(&heap, blocks, TEST_BLOCK_CAPACITY,
        storage.bytes, 128, KERNEL_HEAP_OWNER_FIRST_WORKSPACE, bridge,
        KERNEL_HEAP_BAD_POINTER);
    void *reused = NULL;
    assert(kernel_heap_allocate(&heap, KERNEL_HEAP_OWNER_SESSION, 80, &reused) ==
           KERNEL_HEAP_OK);
    assert(reused == left_hole);
    for (size_t index = 16; index < 96; ++index) {
        assert(storage.bytes[index] == 0);
    }
    assert(heap.free_bytes == 32 && heap.allocated_bytes == 96);
    assert(heap.live_allocations == 2);
    assert(heap.owner_bytes[KERNEL_HEAP_OWNER_KERNEL] == 16);
    assert(heap.owner_bytes[KERNEL_HEAP_OWNER_SESSION] == 80);
    assert(kernel_heap_check(&heap));
    assert_guards(&storage, 0x5c);
}

/* Assertion or sanitizer failure terminates with nonzero status. */
int main(void)
{
    test_owned_page_initialization_and_allocation();
    test_allocation_failures_are_atomic();
    test_initialization_and_corruption_failures();
    test_release_rejections_are_atomic();
    test_fragmentation_coalescing_and_reuse();
    puts("PASS kernel heap: checked free, coalescing, reuse and atomic failures");
}
