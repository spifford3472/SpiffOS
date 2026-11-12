/* Purpose: read-only x86-64 page-table translation for APIC MMIO preflight.
 * Dependencies: C11 integers and a bounded caller-supplied frame mapper.
 * Invariants: no page-table mutation, TLB invalidation, CR3 reload, heap,
 * arbitrary physical dereference or MMIO access. All outputs transactional.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#ifndef X86_PAGE_WALK_H
#define X86_PAGE_WALK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The mapper verifies physical ownership/mappability of one complete 4 KiB
 * page table before returning its 512 little-endian entry bytes. The caller
 * owns the mapping lifetime and must reject MMIO frames as page tables. */
typedef const uint8_t *(*x86_map_table_page)(void *context,
                                              uint64_t physical_page);
struct x86_page_walker {
    x86_map_table_page map_table;
    void *context;
    uint64_t cr3_physical;
    uint8_t paging_levels; /* exactly 4 or 5 */
};

struct x86_page_translation {
    uint64_t physical;
    uint64_t page_size;
    uint64_t leaf_entry;
    bool supervisor_only;
    bool writable;
};

/* Translate one canonical virtual address using only mapped page-table data.
 * Reject missing/non-present intermediate entries, unsupported PS sizes,
 * noncanonical addresses, user-accessible paths and physical overflow.
 * Does not itself decide PAT/MTRR cache type or permission to access MMIO. */
bool x86_page_walk_read_only(const struct x86_page_walker *walker,
                              uint64_t virtual_address,
                              struct x86_page_translation *out);

/* M9-3D5 diagnostic tri-state query (read-only, no MMIO). The result tells
 * truly absent PTEs from missing/invalid page-table mapping authority.
 * ABSENT requires at least one safely mapped table with a nonpresent entry;
 * UNKNOWN must never be mistaken for an absent cache alias. */
enum x86_page_probe_status {
    X86_PAGE_PROBE_UNKNOWN=0,
    X86_PAGE_PROBE_ABSENT=1,
    X86_PAGE_PROBE_PRESENT=2
};
enum x86_page_probe_status x86_page_probe_read_only(
    const struct x86_page_walker *walker, uint64_t virtual_address);
#endif
