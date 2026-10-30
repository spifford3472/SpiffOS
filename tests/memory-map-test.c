/* Purpose: exercise M1 map arithmetic and boot-adapter failure boundaries.
 * Dependencies: host assertions, real inventory/adapter and pinned Limine types.
 * Invariants: synthetic metadata only; never dereference physical frame ranges.
 * Reference: docs/decisions/ADR-memory-foundation.md. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "memory-map.h"
#include "memory-boot.h"
#include "page-access.h"

/* Verify counted usable capacity and immutable caller input on success. */
static void check_valid(struct memory_region *regions, size_t count,
    uint64_t bytes, uint64_t frames)
{
    struct memory_region before[8];
    assert(count <= 8);
    memcpy(before, regions, count * sizeof *regions);
    struct memory_inventory actual = {0};
    assert(memory_map_analyze(regions, count, &actual) == MEMORY_MAP_OK);
    assert(actual.usable_bytes == bytes);
    assert(actual.candidate_frames == frames);
    assert(actual.candidate_bytes == frames * MEMORY_PAGE_BYTES);
    assert(actual.excluded_usable_bytes == bytes - actual.candidate_bytes);
    assert(memcmp(before, regions, count * sizeof *regions) == 0);
}

/* Rejection must leave the caller's prior counters unchanged. */
static void check_invalid(const struct memory_region *regions, size_t count,
    enum memory_map_result expected)
{
    struct memory_inventory before = {11, 22, 33, 44};
    struct memory_inventory actual = before;
    assert(memory_map_analyze(regions, count, &actual) == expected);
    assert(memcmp(&before, &actual, sizeof actual) == 0);
}

/* Exercise empty inputs, holes, ordering, page-zero policy, edge alignment,
 * valid reserved overlap and overlap/overflow at uint64_t boundaries. */
static void test_inventory(void)
{
    struct memory_region map[] = {
        {0x9000, 0x3000, 0}, {0, 0x1000, 0},
        {0x1000, 0x2000, 1}, {0x3000, 0x1000, 0},
        {0x2000, 0x1000, 99}
    };
    check_valid(map, 5, 0x5000, 4);
    struct memory_region edge[] = {{1, 8191, 0}, {UINT64_MAX, 0, 0}};
    check_valid(edge, 2, 8191, 1);
    edge[0] = (struct memory_region){4095, 2, 0};
    check_valid(edge, 2, 2, 0);
    edge[0] = (struct memory_region){0, 4096, 0};
    check_valid(edge, 2, 4096, 0);
    edge[0] = (struct memory_region){UINT64_MAX - 4095, 4095, 0};
    check_valid(edge, 2, 4095, 0);
    struct memory_region reserved[] = {
        {0, UINT64_MAX, UINT64_MAX}, {4096, 8192, 1}
    };
    check_valid(reserved, 2, 0, 0);
    check_invalid(NULL, 1, MEMORY_MAP_INVALID);
    check_invalid(map, 0, MEMORY_MAP_INVALID);
    check_invalid(map, MEMORY_MAP_MAX_ENTRIES + 1, MEMORY_MAP_TOO_MANY);
    assert(memory_map_analyze(map, 5, NULL) == MEMORY_MAP_INVALID);
    struct memory_region overflow = {UINT64_MAX, 1, 1};
    check_invalid(&overflow, 1, MEMORY_MAP_OVERFLOW);
    struct memory_region overlap[] = {
        {4096, 8192, 0}, {8192, 4096, 1}
    };
    check_invalid(overlap, 2, MEMORY_MAP_OVERLAP);
    overlap[1] = (struct memory_region){UINT64_MAX, 2, 1};
    check_invalid(overlap, 2, MEMORY_MAP_OVERFLOW);
    overlap[1] = overlap[0];
    check_invalid(overlap, 2, MEMORY_MAP_OVERLAP);
    overlap[1] = (struct memory_region){4095, 2, 7};
    check_invalid(overlap, 2, MEMORY_MAP_OVERLAP);
    overlap[1] = (struct memory_region){12288, 4096, 0};
    check_valid(overlap, 2, 12288, 3);
    static struct memory_region limit[MEMORY_MAP_MAX_ENTRIES];
    for (size_t index = 0; index < MEMORY_MAP_MAX_ENTRIES; ++index) {
        limit[index] = (struct memory_region){
            (index + 1) * UINT64_C(8192), 4096, 0
        };
    }
    struct memory_inventory total;
    assert(memory_map_analyze(limit, MEMORY_MAP_MAX_ENTRIES, &total) ==
           MEMORY_MAP_OK);
    assert(total.candidate_frames == MEMORY_MAP_MAX_ENTRIES);
}

static char transcript[32768];
static size_t transcript_length, calls, fail_call;

/* Capture borrowed serial text or fail exactly one selected write call. */
static bool capture(const char *text)
{
    if (++calls == fail_call) {
        return false;
    }
    size_t count = strlen(text);
    assert(count < sizeof transcript - transcript_length);
    memcpy(transcript + transcript_length, text, count + 1);
    transcript_length += count;
    return true;
}

/* Reset test-owned callback evidence; no device or allocator state. */
static void reset_capture(size_t failure)
{
    transcript[0] = '\0';
    transcript_length = calls = 0;
    fail_call = failure;
}

/* Check all adapter validation paths and propagation of every serial failure.
 * Invalid metadata can continue boot, but must never claim MEMORY_OK. */
static void test_boot_adapter(void)
{
    struct limine_memmap_entry entry = {4096, 8192, LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *entries[] = {&entry};
    struct limine_memmap_response response = {0, 1, entries};
    reset_capture(0);
    assert(memory_boot_report(&response, capture));
    assert(strcmp(transcript,
        "MEMORY_REGION:4096,8192,0\r\n"
        "MEMORY_SUMMARY:8192,2,8192,0\r\nSPIFFOS_MEMORY_OK\r\n") == 0);
    size_t complete_calls = calls;
    for (size_t failure = 1; failure <= complete_calls; ++failure) {
        reset_capture(failure);
        assert(!memory_boot_report(&response, capture));
        assert(strstr(transcript, "SPIFFOS_MEMORY_OK") == NULL);
    }
    reset_capture(0);
    assert(memory_boot_report(NULL, capture));
    assert(strcmp(transcript, "SPIFFOS_MEMORY_UNAVAILABLE\r\n") == 0);
    assert(!memory_boot_report(&response, NULL));
    for (unsigned int variant = 0; variant < 6; ++variant) {
        struct limine_memmap_response invalid = response;
        if (variant == 0) invalid.revision = 1;
        if (variant == 1) invalid.entry_count = 0;
        if (variant == 2) invalid.entry_count = MEMORY_MAP_MAX_ENTRIES + 1;
        if (variant == 3) invalid.entries = NULL;
        if (variant == 4) entries[0] = NULL;
        if (variant == 5) {
            entries[0] = &entry;
            entry.base = UINT64_MAX;
        }
        reset_capture(0);
        assert(memory_boot_report(&invalid, capture));
        assert(strcmp(transcript, "SPIFFOS_MEMORY_UNAVAILABLE\r\n") == 0);
    }
}

/* Validate that the boot adapter accepts only revision-0 HHDM plus the pinned
 * x86-64 paging modes, derives the correct canonical width, and preserves the
 * caller's prior policy on every unsupported response. No address is touched. */
static void test_page_policy_adapter(void)
{
    struct memory_region region = {0x1000, MEMORY_PAGE_BYTES,
                                   MEMORY_REGION_USABLE};
    uint16_t slot;
    struct frame_allocator allocator = {0};
    assert(frame_allocator_initialize(&allocator, &region, 1, &slot, 1, 0) ==
           FRAME_OK);
    struct limine_hhdm_response hhdm = {
        .revision = 0, .offset = UINT64_C(0xffff800000000000)
    };
    struct limine_paging_mode_response paging = {
        .revision = 0, .mode = LIMINE_PAGING_MODE_X86_64_4LVL
    };
    struct page_access_policy policy = {0};
    assert(memory_boot_page_policy(&hhdm, &paging, &allocator, &policy));
    assert(policy.allocator == &allocator);
    assert(policy.direct_map_offset == hhdm.offset);
    assert(policy.virtual_address_bits == PAGE_ACCESS_X86_64_4_LEVEL_BITS);

    /* A valid direct-map response cannot bypass an unavailable M2 boot pool. */
    reset_capture(0);
    assert(memory_boot_page_probe(&hhdm, &paging, capture));
    assert(strcmp(transcript, "SPIFFOS_PAGE_ACCESS_UNAVAILABLE\r\n") == 0);
    reset_capture(0);
    assert(memory_boot_heap_probe(&hhdm, &paging, capture));
    assert(strcmp(transcript, "SPIFFOS_HEAP_UNAVAILABLE\r\n") == 0);

    paging.mode = LIMINE_PAGING_MODE_X86_64_5LVL;
    assert(memory_boot_page_policy(&hhdm, &paging, &allocator, &policy));
    assert(policy.virtual_address_bits == PAGE_ACCESS_X86_64_5_LEVEL_BITS);
    struct page_access_policy saved = policy;
    paging.mode = 2;
    assert(!memory_boot_page_policy(&hhdm, &paging, &allocator, &policy));
    assert(memcmp(&saved, &policy, sizeof policy) == 0);
    paging.mode = LIMINE_PAGING_MODE_X86_64_4LVL;
    hhdm.revision = 1;
    assert(!memory_boot_page_policy(&hhdm, &paging, &allocator, &policy));
    assert(memcmp(&saved, &policy, sizeof policy) == 0);
    hhdm.revision = 0;
    paging.revision = 1;
    assert(!memory_boot_page_policy(&hhdm, &paging, &allocator, &policy));
    assert(memcmp(&saved, &policy, sizeof policy) == 0);
    assert(!memory_boot_page_policy(NULL, &paging, &allocator, &policy));
    assert(!memory_boot_page_policy(&hhdm, NULL, &allocator, &policy));
    assert(!memory_boot_page_policy(&hhdm, &paging, NULL, &policy));
    assert(!memory_boot_page_policy(&hhdm, &paging, &allocator, NULL));
}

/* Assertions and sanitizers make malformed-map or mutation bugs fail the run. */
int main(void)
{
    test_inventory();
    test_boot_adapter();
    test_page_policy_adapter();
    puts("PASS memory inventory: interval safety, arithmetic, immutable failures and serial adapter");
}
