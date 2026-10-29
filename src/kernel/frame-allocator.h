/* Purpose: internal M2 physical-frame ownership accounting, not a user ABI.
 * Dependencies: stable borrowed M1 regions and caller-owned uint16_t slots.
 * Invariants: one CPU, interrupts disabled, no frame contents/mappings accessed.
 * Reference: docs/decisions/ADR-frame-allocator.md. */
#ifndef SPIFFOS_FRAME_ALLOCATOR_H
#define SPIFFOS_FRAME_ALLOCATOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "memory-map.h"

enum frame_owner {
    FRAME_OWNER_FREE = 0,
    FRAME_OWNER_KERNEL = 1,
    FRAME_OWNER_SESSION = 2,
    FRAME_OWNER_FIRST_WORKSPACE = 3,
    FRAME_OWNER_LAST_WORKSPACE = 11,
    FRAME_OWNER_SUPERVISOR = 12,
    FRAME_OWNER_COUNT = 13
};

enum frame_result {
    FRAME_OK,
    FRAME_INVALID,
    FRAME_MAP_ERROR,
    FRAME_CAPACITY,
    FRAME_OOM,
    FRAME_FORBIDDEN,
    FRAME_BAD_ADDRESS,
    FRAME_DOUBLE_FREE,
    FRAME_WRONG_OWNER
};

struct frame_allocator {
    const struct memory_region *regions;
    size_t region_count;
    uint16_t *owners;
    size_t slot_capacity;
    uint64_t total_frames, reserve_frames;
    uint64_t ordinary_free, reserve_free;
    uint64_t owned[FRAME_OWNER_COUNT];
    bool initialized;
};

/* Initialize a ZEROED, unused caller-owned descriptor. regions and slots must
 * remain valid/exclusively owned throughout its lifetime; input/output storage
 * must not alias. slot_capacity is in uint16_t elements, not bytes. Require
 * storage for ALL candidate frames and reserve <= total. Failure leaves
 * descriptor/slots unchanged. Reinitialization of an active descriptor rejects.
 * Success zeros used slots and records borrowed pointers. No heap/frame writes. */
enum frame_result frame_allocator_initialize(struct frame_allocator *allocator,
    const struct memory_region *regions, size_t count, uint16_t *slots,
    size_t slot_capacity, uint64_t reserve_frames);

/* Allocate exactly one frame and store its physical address on success only.
 * Ordinary requests never use reserve. reserve_request requires SUPERVISOR
 * and uses ONLY reserve. OOM/invalid errors preserve metadata and *address.
 * Borrow initialized exclusive state; address must not alias state/storage.
 * One CPU/IRQs disabled, no physical-frame access or allocation of metadata. */
enum frame_result frame_allocate(struct frame_allocator *allocator,
    enum frame_owner owner, bool reserve_request, uint64_t *address);

/* Release one aligned mapped address owned by owner. Reject out-of-map,
 * unaligned, double and wrong-owner frees before mutation; return to the same
 * ordinary/reserve pool. This internal API has no stale-handle generation.
 * Borrow exclusive state; no candidate-frame reads/writes or heap use. */
enum frame_result frame_release(struct frame_allocator *allocator,
    enum frame_owner owner, uint64_t address);

/* Check stored owner slots against every counter. Read-only and allocation-free.
 * Caller guarantees initialized descriptor/storage are readable and unchanged.
 * One CPU, no interrupts/concurrency. False means invalid/corrupt metadata. */
bool frame_allocator_check(const struct frame_allocator *allocator);

/* Internal cross-file helpers: validate a diagnostic owner token and translate
 * one bounded ordinal to a physical address. Translation writes *address only
 * on success; borrows stable metadata, never dereferences the returned address. */
bool frame_owner_valid(enum frame_owner owner);
bool frame_allocator_address(const struct frame_allocator *allocator,
    uint64_t ordinal, uint64_t *address);

#endif
