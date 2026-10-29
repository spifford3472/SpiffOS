/* Purpose: pure read-only inventory of physical-memory-map intervals.
 * Dependencies: fixed-width types and caller-owned region/summary storage.
 * Invariants: no allocation, frame access, hardware or concurrent mutation.
 * Reference: docs/decisions/ADR-memory-foundation.md (M1 only). */
#ifndef SPIFFOS_MEMORY_MAP_H
#define SPIFFOS_MEMORY_MAP_H
#include <stddef.h>
#include <stdint.h>

enum { MEMORY_PAGE_BYTES = 4096, MEMORY_MAP_MAX_ENTRIES = 512 };
enum { MEMORY_REGION_USABLE = 0 };

struct memory_region {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct memory_inventory {
    uint64_t usable_bytes;
    uint64_t candidate_frames;
    uint64_t candidate_bytes;
    uint64_t excluded_usable_bytes;
};

enum memory_map_result {
    MEMORY_MAP_OK,
    MEMORY_MAP_INVALID,
    MEMORY_MAP_TOO_MANY,
    MEMORY_MAP_OVERFLOW,
    MEMORY_MAP_OVERLAP
};

/* Validate at most MAX_ENTRIES readable intervals and count complete usable
 * 4KiB frames, excluding physical frame zero. Zero-length intervals are ignored.
 * Only type zero is usable. Any overlap involving usable memory is rejected;
 * nonusable-only overlaps are permitted. Input order is immaterial.
 * Borrow all storage synchronously; caller guarantees readable objects and
 * no concurrent mutation. No physical-memory reads/writes or allocation.
 * Failure preserves *summary and all input bytes; success stores all counters.
 * summary must not overlap the input array. NULL/empty input is invalid. */
enum memory_map_result memory_map_analyze(const struct memory_region *regions,
    size_t count, struct memory_inventory *summary);

#endif
