/* Purpose: verify M2 ownership, reserve exhaustion and reversible accounting.
 * Dependencies: real frame allocator/M1 analyzer over synthetic metadata only.
 * Invariants: returned addresses are numbers and are never dereferenced.
 * Reference: docs/decisions/ADR-frame-allocator.md. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "frame-allocator.h"

struct guarded_slots {
    uint8_t before[4], slots[32], after[4];
};

/* Verify live/free counts independently of the allocator's check function. */
static void totals(const struct frame_allocator *allocator)
{
    uint64_t live = 0;
    for (unsigned int owner = 1; owner < FRAME_OWNER_COUNT; ++owner) {
        live += allocator->owned[owner];
    }
    assert(live + allocator->ordinary_free + allocator->reserve_free ==
           allocator->total_frames);
    assert(frame_allocator_check(allocator));
}

/* Initialize on unsorted ranges with holes, a nonusable range and frame zero.
 * Extra slots and guard words remain intentionally outside the managed pool. */
static void test_exhaustion(void)
{
    struct memory_region regions[] = {
        {0x20000, 0x3000, 0}, {0x1000, 0x5000, 0},
        {0, 0x1000, 0}, {0x40000, 0x4000, 0}, {0x10000, 0x1000, 7}
    };
    struct guarded_slots memory;
    memset(&memory, 0xa5, sizeof memory);
    struct frame_allocator allocator = {0}, before;
    uint8_t saved[32];
    memcpy(&before, &allocator, sizeof before);
    memcpy(saved, memory.slots, sizeof saved);
    assert(frame_allocator_initialize(&allocator, regions, 5, memory.slots,
                                     11, 2) == FRAME_CAPACITY);
    assert(memcmp(&before, &allocator, sizeof before) == 0);
    assert(memcmp(saved, memory.slots, sizeof saved) == 0);
    assert(frame_allocator_initialize(&allocator, regions, 5, memory.slots,
                                     32, 13) == FRAME_INVALID);
    assert(memcmp(&before, &allocator, sizeof before) == 0);
    assert(memcmp(saved, memory.slots, sizeof saved) == 0);
    assert(frame_allocator_initialize(&allocator, regions, 5, memory.slots,
                                     32, 2) == FRAME_OK);
    assert(allocator.total_frames == 12 && allocator.ordinary_free == 10 &&
           allocator.reserve_free == 2);
    totals(&allocator);
    uint64_t pages[10], reserve[2];
    for (unsigned int index = 0; index < 10; ++index) {
        enum frame_owner owner = (enum frame_owner)(1 + index);
        assert(frame_allocate(&allocator, owner, false, &pages[index]) == FRAME_OK);
        assert(pages[index] != 0 && pages[index] % 4096 == 0);
        assert(pages[index] != 0x20000 && pages[index] != 0x21000);
        for (unsigned int prior = 0; prior < index; ++prior) {
            assert(pages[index] != pages[prior]);
        }
        totals(&allocator);
    }
    assert(pages[0] == 0x22000); /* Ordinals retain the input region order. */
    uint64_t unchanged = UINT64_C(0xabcdef);
    memcpy(&before, &allocator, sizeof before);
    memcpy(saved, memory.slots, sizeof saved);
    assert(frame_allocate(&allocator, FRAME_OWNER_KERNEL, false, &unchanged) ==
           FRAME_OOM);
    assert(unchanged == UINT64_C(0xabcdef));
    assert(memcmp(&before, &allocator, sizeof before) == 0);
    assert(memcmp(saved, memory.slots, sizeof saved) == 0);
    assert(frame_allocate(&allocator, FRAME_OWNER_KERNEL, true, &unchanged) ==
           FRAME_FORBIDDEN);
    assert(unchanged == UINT64_C(0xabcdef));
    assert(allocator.reserve_free == 2);
    for (unsigned int index = 0; index < 2; ++index) {
        assert(frame_allocate(&allocator, FRAME_OWNER_SUPERVISOR, true,
                              &reserve[index]) == FRAME_OK);
        totals(&allocator);
    }
    assert(reserve[0] == 0x20000 && reserve[1] == 0x21000);
    assert(frame_allocate(&allocator, FRAME_OWNER_SUPERVISOR, true,
                          &unchanged) == FRAME_OOM);
    memcpy(&before, &allocator, sizeof before);
    memcpy(saved, memory.slots, sizeof saved);
    assert(frame_release(&allocator, FRAME_OWNER_SESSION, pages[0]) ==
           FRAME_WRONG_OWNER);
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, 0x10000) ==
           FRAME_BAD_ADDRESS);
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, pages[0] + 1) ==
           FRAME_BAD_ADDRESS);
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, 0) == FRAME_BAD_ADDRESS);
    assert(frame_allocate(&allocator, FRAME_OWNER_FREE, false, &unchanged) ==
           FRAME_INVALID);
    assert(frame_allocate(&allocator, (enum frame_owner)-1, false, &unchanged) ==
           FRAME_INVALID);
    assert(frame_allocate(&allocator, FRAME_OWNER_COUNT, false, &unchanged) ==
           FRAME_INVALID);
    assert(memcmp(&before, &allocator, sizeof before) == 0);
    assert(memcmp(saved, memory.slots, sizeof saved) == 0);
    for (unsigned int index = 0; index < 10; ++index) {
        assert(frame_release(&allocator, (enum frame_owner)(1 + index),
                             pages[index]) == FRAME_OK);
        totals(&allocator);
    }
    for (unsigned int index = 0; index < 2; ++index) {
        assert(frame_release(&allocator, FRAME_OWNER_SUPERVISOR,
                             reserve[index]) == FRAME_OK);
        totals(&allocator);
    }
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, pages[0]) ==
           FRAME_DOUBLE_FREE);
    assert(frame_allocate(&allocator, FRAME_OWNER_KERNEL, false, &unchanged) ==
           FRAME_OK);
    assert(unchanged == pages[0]);
    assert(frame_release(&allocator, FRAME_OWNER_KERNEL, unchanged) == FRAME_OK);
    memcpy(&before, &allocator, sizeof before);
    assert(frame_allocator_initialize(&allocator, regions, 5, memory.slots,
                                     32, 2) == FRAME_INVALID);
    assert(memcmp(&before, &allocator, sizeof before) == 0);
    for (unsigned int index = 12; index < 32; ++index) {
        assert(memory.slots[index] == 0xa5);
    }
    for (unsigned int index = 0; index < 4; ++index) {
        assert(memory.before[index] == 0xa5 && memory.after[index] == 0xa5);
    }
}

/* Cover all group owners, no-reserve mode, all-reserve mode and upper addresses. */
static void test_groups_and_edges(void)
{
    uint8_t slots[32];
    struct memory_region region = {4096, 20 * 4096, 0};
    struct frame_allocator allocator = {0};
    assert(frame_allocator_initialize(&allocator, &region, 1, slots, 32, 3) ==
           FRAME_OK);
    for (unsigned int owner = 1; owner < FRAME_OWNER_COUNT; ++owner) {
        uint64_t address;
        assert(frame_allocate(&allocator, (enum frame_owner)owner, false,
                              &address) == FRAME_OK);
        assert(allocator.owned[owner] == 1);
        totals(&allocator);
    }
    struct frame_allocator reserved = {0};
    struct memory_region reserved_region = region;
    uint8_t reserved_slots[32];
    assert(frame_allocator_initialize(&reserved, &reserved_region, 1,
                                     reserved_slots, 32, 20) == FRAME_OK);
    uint64_t address = 123;
    assert(frame_allocate(&reserved, FRAME_OWNER_SUPERVISOR, false, &address) ==
           FRAME_OOM);
    assert(address == 123 && reserved.reserve_free == 20);
    assert(frame_allocate(&reserved, FRAME_OWNER_SUPERVISOR, true, &address) ==
           FRAME_OK);
    assert(frame_release(&reserved, FRAME_OWNER_SUPERVISOR, address) == FRAME_OK);
    totals(&reserved);

    struct memory_region high_region = {UINT64_MAX - 8191, 4096, 0};
    uint8_t high_slots[32];
    struct frame_allocator high = {0};
    assert(frame_allocator_initialize(&high, &high_region, 1,
                                     high_slots, 32, 0) == FRAME_OK);
    assert(frame_allocate(&high, FRAME_OWNER_KERNEL, false, &address) == FRAME_OK);
    assert(address == UINT64_MAX - 8191);
    assert(frame_release(&high, FRAME_OWNER_KERNEL, address) == FRAME_OK);
    totals(&high);
    assert(frame_allocate(NULL, FRAME_OWNER_KERNEL, false, &address) ==
           FRAME_INVALID);
    struct frame_allocator unused = {0};
    assert(frame_allocate(&unused, FRAME_OWNER_KERNEL, false, &address) ==
           FRAME_INVALID);
    assert(frame_release(NULL, FRAME_OWNER_KERNEL, 4096) == FRAME_INVALID);
    assert(!frame_allocator_check(NULL));
}

/* Assertion failures and ASan/UBSan terminate the test with a nonzero exit. */
int main(void)
{
    test_exhaustion();
    test_groups_and_edges();
    puts("PASS frame allocation: owners, holes, reserve/OOM, rejected frees and counters");
}
