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
    bool fail;
};

/* Translate only the one address authorized by the test policy, require the
 * exact page length, then clear guarded ordinary RAM. Context remains borrowed;
 * returning false performs no write in this test backend. */
static bool guarded_zero(void *opaque, uint64_t mapped_address,
    size_t length_bytes)
{
    struct zero_backend_context *context = opaque;
    ++context->calls;
    if (context->fail || mapped_address != context->expected_address ||
        length_bytes != MEMORY_PAGE_BYTES) {
        return false;
    }
    memset(context->memory->page, 0, length_bytes);
    return true;
}

/* Build a valid allocator with unaligned usable edges, a hole and two owners.
 * The returned descriptor borrows every input, so all storage is supplied by
 * the caller and remains live for the complete test. */
static void initialize_fixture(struct frame_allocator *allocator,
    struct memory_region regions[3], uint16_t slots[8],
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
    uint16_t slots[8];
    struct frame_allocator allocator;
    uint64_t kernel_page, session_page;
    initialize_fixture(&allocator, regions, slots, &kernel_page, &session_page);
    struct page_access_policy policy = {
        .allocator = &allocator,
        .direct_map_offset = UINT64_C(0xffff800000000000),
        .virtual_address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS
    };
    struct frame_allocator saved_allocator = allocator;
    uint16_t saved_slots[8];
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
    uint16_t slots[2];
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
    uint16_t slots[1];
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
    policy.virtual_address_bits = 56;
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_UNSUPPORTED);
    assert(output == physical_address);

    struct memory_region low_region = {0x2000, MEMORY_PAGE_BYTES,
                                       MEMORY_REGION_USABLE};
    struct frame_allocator low_allocator = {0};
    uint16_t low_slots[1];
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
    policy.virtual_address_bits = PAGE_ACCESS_X86_64_5_LEVEL_BITS;
    policy.direct_map_offset = UINT64_C(0x0100000000000000);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_NONCANONICAL);
    policy.direct_map_offset = UINT64_C(0xff00000000000000);
    assert(page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
                               &output) == PAGE_ACCESS_OK);
}

/* Verify the policy alone never touches memory and that the sole injected
 * backend receives exactly one authorized page while surrounding guards stay
 * intact. Backend rejection remains explicit and ownership stays allocated. */
static void test_injected_zero_backend(void)
{
    struct memory_region regions[3];
    uint16_t slots[8];
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
    for (size_t index = 0; index < TEST_GUARD_BYTES; ++index) {
        assert(memory.before[index] == 0xa5 && memory.after[index] == 0xa5);
    }
    assert(allocator.owned[FRAME_OWNER_KERNEL] == 1);

    memset(memory.page, 0x5a, sizeof memory.page);
    backend.fail = true;
    assert(page_access_zero(&policy, FRAME_OWNER_KERNEL, kernel_page,
                            guarded_zero, &backend) ==
           PAGE_ACCESS_BACKEND_FAILED);
    assert(backend.calls == 2 && memory.page[0] == 0x5a);
    assert(page_access_zero(&policy, FRAME_OWNER_SESSION, kernel_page,
                            guarded_zero, &backend) == PAGE_ACCESS_WRONG_OWNER);
    assert(backend.calls == 2);
    assert(page_access_zero(&policy, FRAME_OWNER_KERNEL, kernel_page,
                            NULL, &backend) == PAGE_ACCESS_INVALID);
    assert(allocator.owned[FRAME_OWNER_KERNEL] == 1);
}

/* Assertion or sanitizer failures terminate nonzero; PASS means every focused
 * pure-policy and guarded-backend case above completed. */
int main(void)
{
    test_membership_ownership_and_atomicity();
    test_supervisor_reserve_ownership();
    test_arithmetic_and_canonicality();
    test_injected_zero_backend();
    puts("PASS page access: ownership, membership, arithmetic, canonicality and backend guards");
}
