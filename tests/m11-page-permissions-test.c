/* Purpose: verify M11 private-root and effective mapping checks on the host.
 * Dependencies: C11 assert and a bounded synthetic table mapper.
 * Invariants: fixtures use ordinary C arrays and never change live CPU state.
 * Reference: docs/decisions/ADR-m11-ring3-diagnostic.md. */
#include <assert.h>
#include <stdio.h>

#include "m11-page-permissions.h"

enum { PAGE_BYTES = 4096, ENTRY_BYTES = 8, USER_SLOT = 1 };
static const uint64_t frame_mask = UINT64_C(0x000000fffffff000);
static const uint64_t nx = UINT64_C(1) << 63;
static const uint64_t user_address = UINT64_C(1) << 39;
static uint8_t pages[4][PAGE_BYTES];

/* Return backing for one aligned fixture frame. Other frames are unreadable. */
static const uint8_t *map_page(void *unused, uint64_t physical)
{
    (void)unused;
    if (physical == 0 || physical % PAGE_BYTES != 0 ||
        physical > sizeof pages)
        return NULL;
    return pages[physical / PAGE_BYTES - 1];
}

/* Encode one table entry into owned fixture bytes using x86 byte order. */
static void set_entry(size_t page, size_t index, uint64_t entry)
{
    for (size_t offset = 0; offset < ENTRY_BYTES; ++offset)
        pages[page][index * ENTRY_BYTES + offset] =
            (uint8_t)(entry >> (offset * 8u));
}

/* Verify the chosen slot, full path conjunctions, and transactional outputs. */
int main(void)
{
    struct x86_page_walker walker = {.map_table = map_page, .cr3_physical = 0x1000,
                                     .paging_levels = 4};
    set_entry(0, USER_SLOT, 0x2007);
    set_entry(0, 511, 0x9003);
    set_entry(1, 0, 0x3007);
    set_entry(2, 0, 0x4007);
    set_entry(3, 0, 0x5005);
    set_entry(3, 1, 0x6007 | nx);
    assert(m11_private_root_check(&walker, USER_SLOT, frame_mask));
    set_entry(0, 2, 0x9007);
    assert(!m11_private_root_check(&walker, USER_SLOT, frame_mask));
    set_entry(0, 2, 0);
    set_entry(0, 511, 0x9083);
    assert(!m11_private_root_check(&walker, USER_SLOT, frame_mask));
    set_entry(0, 511, 0x9003);
    assert(!m11_private_root_check(&walker, 256, frame_mask));
    assert(!m11_private_root_check(&walker, USER_SLOT, 0));

    struct m11_page_permissions permission = {0};
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                user_address + 79, &permission) == M11_PAGE_MAPPED);
    assert(permission.physical == 0x5000 + 79);
    assert(permission.user && !permission.writable && permission.executable);
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                user_address + PAGE_BYTES, &permission) == M11_PAGE_MAPPED);
    assert(permission.user && permission.writable && !permission.executable);
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                user_address + 2 * PAGE_BYTES, &permission) == M11_PAGE_ABSENT);
    assert(permission.physical == 0x6000 && permission.writable);
    set_entry(1, 0, 0x3001 | nx);
    set_entry(3, 0, 0x5087); /* PAT at a 4 KiB leaf does not mean huge. */
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                user_address, &permission) == M11_PAGE_MAPPED);
    assert(!permission.user && !permission.writable && !permission.executable);
    set_entry(1, 0, 0x3007 | (UINT64_C(1) << 41));
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                user_address, &permission) == M11_PAGE_UNKNOWN);
    set_entry(1, 0, 0x9007);
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                user_address, &permission) == M11_PAGE_UNKNOWN);
    assert(permission.physical == 0x5000 && !permission.executable);
    set_entry(1, 0, 0x3087);
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                user_address, &permission) == M11_PAGE_UNKNOWN);
    assert(m11_page_permissions(&walker, USER_SLOT, frame_mask,
                                0, &permission) == M11_PAGE_UNKNOWN);
    puts("SPIFFOS_M11_PAGE_PERMISSIONS_OK");
    return 0;
}
