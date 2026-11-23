/* Purpose: validate the private M11 page-table root and user permissions.
 * Dependencies: existing read-only x86_page_walker table mapper and C11.
 * Invariants: borrowed table bytes only; no hardware state or table changes.
 * Reference: docs/decisions/ADR-m11-ring3-diagnostic.md. */
#include "m11-page-permissions.h"

enum {
    M11_TABLE_ENTRIES = 512,
    M11_ENTRY_BYTES = 8,
    M11_LOWER_HALF_SLOTS = 256,
    M11_PRESENT = 1,
    M11_WRITABLE = 1 << 1,
    M11_USER = 1 << 2,
    M11_PAGE_SIZE_BIT = 1 << 7
};
static const uint64_t m11_arch_frame_bits = UINT64_C(0x000ffffffffff000);

/* Decode a table slot without requiring aligned or host-endian memory.
 * The mapper owns table storage and has validated all 4 KiB. */
static uint64_t m11_read_entry(const uint8_t *table, size_t slot)
{
    uint64_t entry = 0;
    for (size_t byte = 0; byte < M11_ENTRY_BYTES; ++byte)
        entry |= (uint64_t)table[slot * M11_ENTRY_BYTES + byte] << (8u * byte);
    return entry;
}

/* Require a real aligned root and a contiguous physical address mask whose
 * zero low 12 bits represent the page offset. Maps and modifies nothing. */
static bool m11_walk_inputs(const struct x86_page_walker *walker,
                            size_t user_slot, uint64_t frame_mask)
{
    if (walker == NULL || walker->map_table == NULL ||
        walker->paging_levels != 4 || user_slot >= M11_LOWER_HALF_SLOTS ||
        frame_mask == 0 || (frame_mask & ~m11_arch_frame_bits) != 0 ||
        walker->cr3_physical == 0 || (walker->cr3_physical & ~frame_mask) != 0)
        return false;
    uint64_t byte_address_mask = frame_mask | UINT64_C(0xfff);
    return (byte_address_mask & (byte_address_mask + 1u)) == 0;
}

/* Verify all root entries from the root frame itself. A missing entry is
 * valid only outside the selected slot. The unused user-side root entries
 * must be supervisor-only if present, just like the higher-half entries. */
bool m11_private_root_check(const struct x86_page_walker *walker,
                            size_t user_slot, uint64_t frame_mask)
{
    if (!m11_walk_inputs(walker, user_slot, frame_mask))
        return false;
    const uint8_t *root = walker->map_table(walker->context, walker->cr3_physical);
    if (root == NULL)
        return false;
    for (size_t slot = 0; slot < M11_TABLE_ENTRIES; ++slot) {
        uint64_t entry = m11_read_entry(root, slot);
        if ((entry & M11_PRESENT) == 0) {
            if (slot == user_slot)
                return false;
            continue;
        }

        uint64_t frame = entry & m11_arch_frame_bits;
        if ((entry & M11_PAGE_SIZE_BIT) != 0 || frame == 0 ||
            (frame & ~frame_mask) != 0 ||
            ((entry & M11_USER) != 0) != (slot == user_slot))
            return false;
    }
    return true;
}

/* Walk one address in the selected private branch. No user virtual
 * address is dereferenced. W and U must be allowed at every level, and
 * an NX bit anywhere disallows execution. The physical offset is added
 * only after proving a 4 KiB leaf, whose bit 7 encodes PAT, not page size. */
enum m11_page_status m11_page_permissions(const struct x86_page_walker *walker,
    size_t user_slot, uint64_t frame_mask, uint64_t address,
    struct m11_page_permissions *out)
{
    enum { PAGE_SHIFT = 12, INDEX_BITS = 9, TOP_SHIFT = 39 };
    const uint64_t nx_bit = UINT64_C(1) << 63;
    if (out == NULL || !m11_walk_inputs(walker, user_slot, frame_mask) ||
        (address >> TOP_SHIFT) != user_slot)
        return M11_PAGE_UNKNOWN;

    uint64_t frame = walker->cr3_physical;
    struct m11_page_permissions result = {
        .user = true, .writable = true, .executable = true
    };
    for (unsigned shift = TOP_SHIFT; ; shift -= INDEX_BITS) {
        const uint8_t *table = walker->map_table(walker->context, frame);
        if (table == NULL)
            return M11_PAGE_UNKNOWN;
        size_t slot = (size_t)((address >> shift) & (M11_TABLE_ENTRIES - 1u));
        uint64_t entry = m11_read_entry(table, slot);
        if ((entry & M11_PRESENT) == 0)
            return M11_PAGE_ABSENT;

        frame = entry & m11_arch_frame_bits;
        bool leaf = shift == PAGE_SHIFT;
        if (frame == 0 || (frame & ~frame_mask) != 0 ||
            (!leaf && (entry & M11_PAGE_SIZE_BIT) != 0))
            return M11_PAGE_UNKNOWN;

        result.user = result.user && (entry & M11_USER) != 0;
        result.writable = result.writable && (entry & M11_WRITABLE) != 0;
        result.executable = result.executable && (entry & nx_bit) == 0;
        if (leaf) {
            result.physical = frame | (address & ((UINT64_C(1) << PAGE_SHIFT) - 1u));
            *out = result;
            return M11_PAGE_MAPPED;
        }
    }
}
