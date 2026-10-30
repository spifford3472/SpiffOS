/* Purpose: authorize checked access to one M2-owned physical page.
 * Responsibilities: validate ownership, usable-frame membership, direct-map
 * arithmetic and x86-64 canonical addresses before a narrow backend touches RAM.
 * Dependencies: M1 memory regions and an initialized M2 frame allocator.
 * Invariants: borrowed metadata stays stable; one boot CPU, IRQs disabled;
 * numeric physical/direct-map addresses are never authority by themselves.
 * Reference: docs/decisions/ADR-memory-services.md, M3. */
#ifndef SPIFFOS_PAGE_ACCESS_H
#define SPIFFOS_PAGE_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "frame-allocator.h"

enum page_access_result {
    PAGE_ACCESS_OK,
    PAGE_ACCESS_INVALID,
    PAGE_ACCESS_UNSUPPORTED,
    PAGE_ACCESS_BAD_PHYSICAL_ADDRESS,
    PAGE_ACCESS_WRONG_OWNER,
    PAGE_ACCESS_ADDRESS_OVERFLOW,
    PAGE_ACCESS_NONCANONICAL,
    PAGE_ACCESS_BACKEND_FAILED
};

enum page_access_virtual_bits {
    PAGE_ACCESS_X86_64_4_LEVEL_BITS = 48,
    PAGE_ACCESS_X86_64_5_LEVEL_BITS = 57
};

struct page_access_policy {
    const struct frame_allocator *allocator;
    uint64_t direct_map_offset;
    uint8_t virtual_address_bits;
};

/* A backend receives one already-authorized direct-map address and exactly one
 * page length in bytes. It borrows context synchronously and returns true only
 * after zeroing the complete range. The kernel implementation is the sole M3
 * code allowed to convert mapped_address to a C pointer; host tests instead
 * translate it into guarded test RAM. No allocation or ownership transfer. */
typedef bool (*page_access_zero_backend)(void *context,
    uint64_t mapped_address, size_t length_bytes);

/* Resolve one aligned, complete, currently allocated USABLE page belonging to
 * owner. Policy and allocator/map/slots are borrowed and immutable for the
 * call. Validate allocator consistency, exact owner, physical/direct-map range
 * overflow and 48- or 57-bit canonicality. Success writes mapped_address;
 * every error preserves it and all allocator state. Pure: no RAM access,
 * allocation, pointer conversion, logging, locks or interrupt handling. */
enum page_access_result page_access_resolve(
    const struct page_access_policy *policy, enum frame_owner owner,
    uint64_t physical_address, uint64_t *mapped_address);

/* Resolve with the same checks, then ask backend to zero exactly 4096 bytes.
 * A false backend result becomes PAGE_ACCESS_BACKEND_FAILED; ownership remains
 * allocated so the higher-level allocate-and-zero caller can roll it back with
 * the exact owner. No address is published by this function. The backend may
 * have partially written on failure, so callers must never expose that page.
 * Borrow all inputs synchronously; one boot CPU, interrupts disabled. */
enum page_access_result page_access_zero(
    const struct page_access_policy *policy, enum frame_owner owner,
    uint64_t physical_address, page_access_zero_backend backend,
    void *backend_context);

#endif
