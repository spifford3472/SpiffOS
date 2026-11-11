/* Purpose: sanitizer tests for pure M3 checked page-access authorization.
 * Responsibilities: exercise ownership, full-frame membership, alignment,
 * overflow, canonical addressing and the injected zero backend with guards.
 * Dependencies: M1/M2 pure metadata code and ordinary caller-owned host RAM.
 * Invariants: physical/mapped values remain numbers until the test backend
 * explicitly translates one expected authorized address into its guarded RAM.
 * Reference: docs/decisions/ADR-memory-services.md, M3 page-access contract. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "page-access.h"

enum { TEST_GUARD_BYTES = 32 };

struct guarded_page {
    unsigned char before[TEST_GUARD_BYTES];
    unsigned char page[MEMORY_PAGE_BYTES];
    unsigned char after[TEST_GUARD_BYTES];
};

struct zero_backend_context {
    uint64_t expected_address;
    struct guarded_page *memory;
    size_t calls;
    size_t bytes_before_failure;
    bool fail;
};

/* Translate only the numeric mapped_address authorized by the fixture and
 * require length_bytes == 4096 before clearing borrowed guarded host RAM from
 * its first page byte. opaque must point to a live zero_backend_context. Return
 * true only after a complete clear; a configured failure returns false after a
 * bounded partial write. Calls are counted even on rejection. No allocation or
 * ownership transfer; single-threaded host test only, with no interrupts. */
static bool guarded_zero(void *opaque, uint64_t mapped_address,
    size_t length_bytes)
{
    struct zero_backend_context *context = opaque;
    ++context->calls;
    if (mapped_address != context->expected_address ||
        length_bytes != MEMORY_PAGE_BYTES ||
        context->bytes_before_failure > length_bytes) {
        return false;
    }
    size_t bytes_to_clear = context->fail ?
        context->bytes_before_failure : length_bytes;
    memset(context->memory->page, 0, bytes_to_clear);
    return !context->fail;
}

struct corrupt_backend_context {
    uint8_t *allocated_slot;
    size_t calls;
};

/* Deliberately violate the backend non-aliasing contract after authorization
 * to reach the otherwise defensive rollback-failure branch. opaque borrows a
 * live corrupt_backend_context; mapped_address is intentionally unused and
 * length_bytes must be exactly one page. Mutate the named slot, count one call
 * and return false. The test restores its fixture immediately afterward;
 * production callers must retire a descriptor after ROLLBACK_FAILED. No heap,
 * ownership transfer, concurrency or interrupt-context use. */
static bool corrupt_owner_then_fail(void *opaque, uint64_t mapped_address,
    size_t length_bytes)
{
    struct corrupt_backend_context *context = opaque;
    (void)mapped_address;
    assert(length_bytes == MEMORY_PAGE_BYTES);
    ++context->calls;
    *context->allocated_slot = FRAME_OWNER_SESSION;
    return false;
}

/* Require every byte in both sentinels around borrowed test-owned page storage
 * to equal expected. Return normally on success; an assertion terminates the
 * host test on mismatch. Read-only, no allocator/backend/ownership side effects,
 * allocation, concurrency or interrupt-context use. */
static void assert_guards_equal(const struct guarded_page *memory,
    unsigned char expected)
{
    for (size_t index = 0; index < TEST_GUARD_BYTES; ++index) {
        assert(memory->before[index] == expected);
        assert(memory->after[index] == expected);
    }
}

/* Build a valid allocator with unaligned usable edges, a hole and two owners.
 * The returned descriptor borrows every input, so all storage is supplied by
 * the caller and remains live for the complete test. */
static void initialize_fixture(struct frame_allocator *allocator,
    struct memory_region regions[3], uint8_t slots[8],
    uint64_t *kernel_page, uint64_t *session_page)
{
    regions[0] = (struct memory_region){0x1080, 0x3f80, MEMORY_REGION_USABLE};
    regions[1] = (struct memory_region){0x9000, 0x1000, 7};
    regions[2] = (struct memory_region){0x10000, 0x2000, MEMORY_REGION_USABLE};
    *allocator = (struct frame_allocator){0};
    assert(frame_allocator_initialize(allocator, regions, 3, slots, 8, 0) ==
           FRAME_OK);
    assert(allocator->total_frames == 5);
    assert(frame_allocate(allocator, FRAME_OWNER_KERNEL, false, kernel_page) ==
           FRAME_OK);
    assert(frame_allocate(allocator, FRAME_OWNER_SESSION, false, session_page) ==
           FRAME_OK);
    assert(*kernel_page == 0x2000 && *session_page == 0x3000);
}

/* Prove exact ownership and complete candidate membership are required while
 * all failures preserve caller output and M2 metadata/counters. */
static void test_membership_ownership_and_atomicity(void)
{
    struct memory_region regions[3];
    uint8_t slots[8];
    struct frame_allocator allocator;
    uint64_t kernel_page, session_page;
    initialize_fixture(&allocator, regions, slots, &kernel_page, &session_page);
    struct page_access_policy policy = {
        .allocator = &allocator,
        .direct_map_offset = UINT64_C(0xffff800000000000),
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    struct frame_allocator saved_allocator = allocator;
    uint8_t saved_slots[8];
    memcpy(saved_slots, slots, sizeof slots);
    uint64_t output = UINT64_C(0x1122334455667788);

    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page,
                               &output) == PAGE_ACCESS_OK);
    assert(output == UINT64_C(0xffff800000002000));
    output = UINT64_C(0x1122334455667788);
    assert(page_access_resolve(&policy, FRAME_OWNER_SESSION, kernel_page,
                               &output) == PAGE_ACCESS_WRONG_OWNER);
    assert(output == UINT64_C(0x1122334455667788));
    assert(page_access_resolve(&policy, FRAME_OWNER_FREE, kernel_page,
                               &output) == PAGE_ACCESS_INVALID);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page + 1,
                               &output) == PAGE_ACCESS_BAD_PHYSICAL_ADDRESS);
    /* 0x1000 is only a partial page at the first usable region's edge. */
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, 0x1000,
                               &output) == PAGE_ACCESS_BAD_PHYSICAL_ADDRESS);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, 0x9000,
                               &output) == PAGE_ACCESS_BAD_PHYSICAL_ADDRESS);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, 0x10000,
                               &output) == PAGE_ACCESS_WRONG_OWNER);
    assert(output == UINT64_C(0x1122334455667788));
    assert(memcmp(&saved_allocator, &allocator, sizeof allocator) == 0);
    assert(memcmp(saved_slots, slots, sizeof slots) == 0);

    /* Counter/slot disagreement is rejected before ownership is trusted. */
    ++allocator.ordinary_free;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page,
                               &output) == PAGE_ACCESS_INVALID);
    allocator = saved_allocator;
    /* A changed map whose candidate count no longer matches M2 is invalid. */
    regions[2].length = 0x1000;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page,
                               &output) == PAGE_ACCESS_INVALID);
    regions[2].length = 0x2000;

    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, kernel_page) == FRAME_OK);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page,
                               &output) == PAGE_ACCESS_WRONG_OWNER);
}

/* Prove reserve ordinals remain supervisor-only: exact supervisor ownership is
 * accepted, another requested owner is rejected, and a corrupted reserve slot
 * cannot become authorization even if it contains a valid owner token. */
static void test_supervisor_reserve_ownership(void)
{
    struct memory_region region = {0x4000, 2 * MEMORY_PAGE_BYTES,
                                   MEMORY_REGION_USABLE};
    uint8_t slots[2];
    struct frame_allocator allocator = {0};
    assert(frame_allocator_initialize(&allocator, &region, 1, slots, 2, 1) ==
           FRAME_OK);
    uint64_t reserve_page;
    assert(frame_allocate(&allocator, FRAME_OWNER_SUPERVISOR, true,
                          &reserve_page) == FRAME_OK);
    struct page_access_policy policy = {
        .allocator = &allocator,
        .direct_map_offset = 0,
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    uint64_t output = 0;
    assert(page_access_resolve(&policy, FRAME_OWNER_SUPERVISOR, reserve_page,
                               &output) == PAGE_ACCESS_OK);
    assert(output == reserve_page);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, reserve_page,
                               &output) == PAGE_ACCESS_WRONG_OWNER);

    slots[0] = FRAME_OWNER_KERNEL;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, reserve_page,
                               &output) == PAGE_ACCESS_INVALID);
}

/* Exercise both supported canonical widths, their holes, and physical/mapped
 * half-open range overflow without ever converting the results to pointers. */
static void test_arithmetic_and_canonicality(void)
{
    struct memory_region region = {
        UINT64_MAX - UINT64_C(8191), MEMORY_PAGE_BYTES, MEMORY_REGION_USABLE
    };
    uint8_t slots[1];
    struct frame_allocator allocator = {0};
    uint64_t physical_address;
    assert(frame_allocator_initialize(&allocator, &region, 1, slots, 1, 0) ==
           FRAME_OK);
    assert(frame_allocate(&allocator, FRAME_OWNER_KERNEL, false,
                          &physical_address) == FRAME_OK);
    assert(physical_address == UINT64_MAX - UINT64_C(8191));
    struct page_access_policy policy = {
        .allocator = &allocator,
        .direct_map_offset = 0,
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    uint64_t output = UINT64_C(0xa5a5a5a5a5a5a5a5);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
    assert(output == physical_address);
    policy.direct_map_offset = MEMORY_PAGE_BYTES;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_ADDRESS_OVERFLOW);
    /* The prior case fits the mapped start but overflows its exclusive end;
     * this larger offset overflows the mapped start addition itself. */
    policy.direct_map_offset = 2 * MEMORY_PAGE_BYTES;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_ADDRESS_OVERFLOW);
    policy.virtual_address_bits = 56;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_UNSUPPORTED);
    assert(output == physical_address);

    struct memory_region low_region = {0x2000, MEMORY_PAGE_BYTES,
                                       MEMORY_REGION_USABLE};
    struct frame_allocator low_allocator = {0};
    uint8_t low_slots[1];
    assert(frame_allocator_initialize(&low_allocator, &low_region, 1,
                                      low_slots, 1, 0) == FRAME_OK);
    assert(frame_allocate(&low_allocator, FRAME_OWNER_KERNEL, false,
                          &physical_address) == FRAME_OK);
    policy.allocator = &low_allocator;
    policy.virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS;
    /* The first byte is canonical, but the page straddles into the hole. */
    policy.direct_map_offset =
        UINT64_C(0x00007fffffffffff) - UINT64_C(2047) - physical_address;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_NONCANONICAL);
    policy.direct_map_offset = UINT64_C(0x0000800000000000);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_NONCANONICAL);
    policy.direct_map_offset = UINT64_C(0xffff800000000000);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
    /* Exact low/high canonical page boundaries are valid in four-level mode. */
    policy.direct_map_offset =
        UINT64_C(0x00007ffffffff000) - physical_address;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
    assert(output == UINT64_C(0x00007ffffffff000));
    policy.direct_map_offset =
        UINT64_C(0xffff800000000000) - physical_address;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
    assert(output == UINT64_C(0xffff800000000000));
    policy.virtual_address_bits = PAGE_ACCESS_X86_64_5_LEVEL_BITS;
    policy.direct_map_offset = UINT64_C(0x0100000000000000);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_NONCANONICAL);
    policy.direct_map_offset = UINT64_C(0xff00000000000000);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
    /* Repeat the exact page-boundary proof for five-level canonical space. */
    policy.direct_map_offset =
        UINT64_C(0x00fffffffffff000) - physical_address;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
    assert(output == UINT64_C(0x00fffffffffff000));
    policy.direct_map_offset =
        UINT64_C(0xff00000000000000) - physical_address;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
    assert(output == UINT64_C(0xff00000000000000));
}

/* Verify the policy alone never touches memory and that the sole injected
 * backend receives exactly one authorized page while surrounding guards stay
 * intact. Backend rejection remains explicit and ownership stays allocated. */
static void test_injected_zero_backend(void)
{
    struct memory_region regions[3];
    uint8_t slots[8];
    struct frame_allocator allocator;
    uint64_t kernel_page, session_page;
    initialize_fixture(&allocator, regions, slots, &kernel_page, &session_page);
    (void)session_page;
    struct page_access_policy policy = {
        .allocator = &allocator,
        .direct_map_offset = UINT64_C(0xffff800000000000),
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    struct guarded_page memory;
    memset(&memory, 0xa5, sizeof memory);
    struct zero_backend_context backend = {
        .expected_address = policy.direct_map_offset + kernel_page,
        .memory = &memory
    };
    assert(page_access_zero(&policy, FRAME_OWNER_KERNEL, kernel_page,
                            guarded_zero, &backend) == PAGE_ACCESS_OK);
    assert(backend.calls == 1);
    for (size_t index = 0; index < MEMORY_PAGE_BYTES; ++index) {
        assert(memory.page[index] == 0);
    }
    assert_guards_equal(&memory, 0xa5);
    assert(allocator.owned[FRAME_OWNER_KERNEL] == 1);

    memset(memory.page, 0x5a, sizeof memory.page);
    backend.fail = true;
    backend.bytes_before_failure = 137;
    assert(page_access_zero(&policy, FRAME_OWNER_KERNEL, kernel_page,
                            guarded_zero, &backend) ==
           PAGE_ACCESS_BACKEND_FAILED);
    assert(backend.calls == 2 && memory.page[0] == 0);
    assert(memory.page[136] == 0 && memory.page[137] == 0x5a);
    assert_guards_equal(&memory, 0xa5);
    assert(page_access_zero(&policy, FRAME_OWNER_SESSION, kernel_page,
                            guarded_zero, &backend) == PAGE_ACCESS_WRONG_OWNER);
    assert(backend.calls == 2);
    assert(page_access_zero(&policy, FRAME_OWNER_KERNEL, kernel_page,
                            NULL, &backend) == PAGE_ACCESS_INVALID);
    assert(allocator.owned[FRAME_OWNER_KERNEL] == 1);
}

/* Prove the higher-level operation publishes only a fully zeroed allocation
 * and rolls ownership/counters back when the backend rejects initialization.
 * The guarded backend translates only the first currently free fixture page. */
static void test_allocate_zero_and_rollback(void)
{
    struct memory_region regions[3];
    uint8_t slots[8];
    struct frame_allocator allocator;
    uint64_t kernel_page, session_page;
    initialize_fixture(&allocator, regions, slots, &kernel_page, &session_page);
    (void)kernel_page;
    (void)session_page;
    struct page_access_policy policy = {
        .allocator = &allocator,
        .direct_map_offset = UINT64_C(0xffff800000000000),
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    struct guarded_page memory;
    memset(&memory, 0x6b, sizeof memory);
    struct zero_backend_context backend = {
        .expected_address = policy.direct_map_offset + UINT64_C(0x4000),
        .memory = &memory,
        .bytes_before_failure = 251,
        .fail = true
    };
    uint64_t free_before = allocator.ordinary_free;
    uint64_t owned_before = allocator.owned[FRAME_OWNER_KERNEL];
    uint64_t output = UINT64_C(0x1122334455667788);
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, &output) == PAGE_ACCESS_BACKEND_FAILED);
    assert(output == UINT64_C(0x1122334455667788));
    assert(allocator.ordinary_free == free_before);
    assert(allocator.owned[FRAME_OWNER_KERNEL] == owned_before);
    assert(frame_allocator_check(&allocator));
    assert(memory.page[0] == 0 && memory.page[250] == 0);
    assert(memory.page[251] == 0x6b);
    assert_guards_equal(&memory, 0x6b);

    backend.fail = false;
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, &output) == PAGE_ACCESS_OK);
    assert(output == UINT64_C(0x4000));
    assert(allocator.ordinary_free == free_before - 1);
    assert(allocator.owned[FRAME_OWNER_KERNEL] == owned_before + 1);
    for (size_t index = 0; index < MEMORY_PAGE_BYTES; ++index) {
        assert(memory.page[index] == 0);
    }
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, output) == FRAME_OK);
    assert(allocator.ordinary_free == free_before);
    assert(frame_allocator_check(&allocator));

    /* Dirty the released backing store, then prove first-fit reuse clears the
     * complete page again before republishing the same physical address. */
    memset(memory.page, 0xcc, sizeof memory.page);
    output = UINT64_C(0x8877665544332211);
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, &output) == PAGE_ACCESS_OK);
    assert(output == UINT64_C(0x4000));
    for (size_t index = 0; index < MEMORY_PAGE_BYTES; ++index) {
        assert(memory.page[index] == 0);
    }
    assert_guards_equal(&memory, 0x6b);
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, output) == FRAME_OK);

    struct page_access_policy other_policy = policy;
    other_policy.allocator = NULL;
    output = UINT64_C(0x8877665544332211);
    assert(page_access_allocate_zero(&allocator, &other_policy,
               FRAME_OWNER_KERNEL, guarded_zero, &backend, &output) ==
           PAGE_ACCESS_INVALID);
    assert(output == UINT64_C(0x8877665544332211));
}

/* Cover invalid/null publications, pre-backend resolve rollback, ordinary-pool
 * exhaustion and defensive rollback-failure reporting. This no-argument host
 * test returns normally only when all assertions pass. Its allocator, map,
 * slots, outputs and guarded bytes are stack-owned for the call. Each ordinary
 * failure preserves output, counters and guards; only the deliberately contract-
 * violating backend corrupts metadata, which is restored before returning.
 * Single-threaded test only, with no heap, hardware or interrupt context. */
static void test_failure_path_matrix(void)
{
    struct memory_region regions[3];
    uint8_t slots[8];
    struct frame_allocator allocator;
    uint64_t kernel_page, session_page;
    initialize_fixture(&allocator, regions, slots, &kernel_page, &session_page);
    (void)session_page;
    struct page_access_policy policy = {
        .allocator = &allocator,
        .direct_map_offset = UINT64_C(0xffff800000000000),
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    struct guarded_page memory;
    memset(&memory, 0x4d, sizeof memory);
    struct zero_backend_context backend = {
        .expected_address = policy.direct_map_offset + UINT64_C(0x4000),
        .memory = &memory
    };
    struct frame_allocator saved_allocator = allocator;
    uint8_t saved_slots[8];
    memcpy(saved_slots, slots, sizeof slots);

    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page,
                               NULL) == PAGE_ACCESS_INVALID);
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, NULL) == PAGE_ACCESS_INVALID);
    uint64_t output = UINT64_C(0x123456789abcdef0);
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_FREE,
               guarded_zero, &backend, &output) == PAGE_ACCESS_INVALID);
    assert(output == UINT64_C(0x123456789abcdef0) && backend.calls == 0);
    assert(memcmp(&saved_allocator, &allocator, sizeof allocator) == 0);
    assert(memcmp(saved_slots, slots, sizeof slots) == 0);
    assert_guards_equal(&memory, 0x4d);

    /* Result publication must not overwrite any metadata that authorized it.
     * Exact field aliases exercise the policy and allocator ranges without
     * forming a misaligned uint64_t pointer into the smaller owner slots. */
    uint64_t saved_offset = policy.direct_map_offset;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page,
               &policy.direct_map_offset) == PAGE_ACCESS_INVALID);
    assert(policy.direct_map_offset == saved_offset);
    struct memory_region saved_first_region = regions[0];
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, kernel_page,
               &regions[0].base) == PAGE_ACCESS_INVALID);
    assert(memcmp(&saved_first_region, &regions[0], sizeof regions[0]) == 0);
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, &allocator.total_frames) ==
           PAGE_ACCESS_INVALID);
    assert(memcmp(&saved_allocator, &allocator, sizeof allocator) == 0);
    assert(memcmp(saved_slots, slots, sizeof slots) == 0);
    assert(backend.calls == 0);

    /* Corrupt accounting is rejected before frame_allocate() can mutate an
     * ownership slot or counter; the later resolver is not the first guard. */
    ++allocator.ordinary_free;
    struct frame_allocator corrupt_allocator = allocator;
    output = UINT64_C(0x123456789abcdef0);
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, &output) == PAGE_ACCESS_INVALID);
    assert(memcmp(&corrupt_allocator, &allocator, sizeof allocator) == 0);
    assert(memcmp(saved_slots, slots, sizeof slots) == 0);
    assert(output == UINT64_C(0x123456789abcdef0) && backend.calls == 0);
    allocator = saved_allocator;

    /* Allocation succeeds, unsupported policy rejects resolution before the
     * backend, and the exact-owner release restores all M2 state. */
    policy.virtual_address_bits = 56;
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, &output) == PAGE_ACCESS_UNSUPPORTED);
    assert(output == UINT64_C(0x123456789abcdef0) && backend.calls == 0);
    assert(memcmp(&saved_allocator, &allocator, sizeof allocator) == 0);
    assert(memcmp(saved_slots, slots, sizeof slots) == 0);
    assert_guards_equal(&memory, 0x4d);
    policy.virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS;

    /* Consume all remaining ordinary frames without touching reserve state. */
    uint64_t remaining[3];
    for (size_t index = 0; index < 3; ++index) {
        assert(frame_allocate(&allocator, FRAME_OWNER_SESSION, false,
                              &remaining[index]) == FRAME_OK);
    }
    uint64_t ordinary_owned = allocator.owned[FRAME_OWNER_SESSION];
    assert(allocator.ordinary_free == 0);
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               guarded_zero, &backend, &output) ==
           PAGE_ACCESS_ALLOCATION_FAILED);
    assert(output == UINT64_C(0x123456789abcdef0) && backend.calls == 0);
    assert(allocator.ordinary_free == 0);
    assert(allocator.owned[FRAME_OWNER_SESSION] == ordinary_owned);
    assert(frame_allocator_check(&allocator));
    assert_guards_equal(&memory, 0x4d);

    /* Restore a valid free frame, then force frame_release() to reject the
     * rollback by corrupting the allocated slot from inside a hostile backend. */
    assert(frame_release(&allocator, FRAME_OWNER_SESSION, remaining[0]) ==
           FRAME_OK);
    struct frame_allocator before_corruption = allocator;
    uint8_t slots_before_corruption[8];
    memcpy(slots_before_corruption, slots, sizeof slots);
    struct corrupt_backend_context corrupt = {
        .allocated_slot = &slots[2]
    };
    assert(page_access_allocate_zero(&allocator, &policy, FRAME_OWNER_KERNEL,
               corrupt_owner_then_fail, &corrupt, &output) ==
           PAGE_ACCESS_ROLLBACK_FAILED);
    assert(output == UINT64_C(0x123456789abcdef0) && corrupt.calls == 1);
    allocator = before_corruption;
    memcpy(slots, slots_before_corruption, sizeof slots);
    assert(frame_allocator_check(&allocator));
}

/* Assertion or sanitizer failures terminate nonzero; PASS means every focused
 * pure-policy and guarded-backend case above completed. */
int main(void)
{
    test_membership_ownership_and_atomicity();
    test_supervisor_reserve_ownership();
    test_arithmetic_and_canonicality();
    test_injected_zero_backend();
    test_allocate_zero_and_rollback();
    test_failure_path_matrix();
    puts("PASS page access: ownership, null/OOM failures, rollback, boundaries and backend guards");
}
