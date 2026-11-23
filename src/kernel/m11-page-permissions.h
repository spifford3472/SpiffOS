/* Purpose: verify effective permissions of the isolated M11 4KiB mapping.
 * Dependencies: the existing read-only page-table mapper interface.
 * Invariants: no CPU state change, table writes, or allocation.
 * Reference: docs/decisions/ADR-m11-ring3-diagnostic.md. */
#ifndef SPIFFOS_M11_PAGE_PERMISSIONS_H
#define SPIFFOS_M11_PAGE_PERMISSIONS_H

#include "x86-page-walk.h"

enum m11_page_status {
    M11_PAGE_UNKNOWN,
    M11_PAGE_ABSENT,
    M11_PAGE_MAPPED
};

struct m11_page_permissions {
    uint64_t physical;
    bool user;
    bool writable;
    bool executable;
};

/* Check that the chosen lower-half PML4 slot is the only user-enabled
 * top-level entry. The borrowed walker maps read-only table bytes. The
 * frame mask contains the hardware-supported frame address bits. */
bool m11_private_root_check(const struct x86_page_walker *walker,
                            size_t user_slot, uint64_t frame_mask);

/* Check one byte address inside that slot using only 4 KiB leaf mappings.
 * CPU NXE and four-level mode are preconditions checked by the guest.
 * Return ABSENT for a nonpresent entry or UNKNOWN for invalid input or an
 * unreadable/malformed table. Modify caller-owned out only on MAPPED. */
enum m11_page_status m11_page_permissions(const struct x86_page_walker *walker,
    size_t user_slot, uint64_t frame_mask, uint64_t address,
    struct m11_page_permissions *out);

#endif /* SPIFFOS_M11_PAGE_PERMISSIONS_H */
