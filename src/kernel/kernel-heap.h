/* Purpose: define the bounded internal M4-M5 kernel-heap contract.
 * Responsibilities: describe caller-owned arena/metadata storage, owner groups,
 * explicit results, allocation/free, lifecycle cleanup and invariant checks.
 * Dependencies: fixed-width C types only; a later implementation may consume
 * M3-validated, kernel-accessible backing memory supplied by its caller.
 * Invariants: metadata is external to the arena; one boot CPU owns all state
 * with interrupts disabled; this is not a user ABI or virtual-memory design.
 * Reference: docs/decisions/ADR-memory-services.md, M4 heap design. */
#ifndef SPIFFOS_KERNEL_HEAP_H
#define SPIFFOS_KERNEL_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    KERNEL_HEAP_ALIGNMENT_BYTES = 16,
    KERNEL_HEAP_MIN_ALLOCATION_BYTES = 16,
    KERNEL_HEAP_MAX_ALLOCATION_BYTES = 1024 * 1024,
    KERNEL_HEAP_MAX_ARENA_BYTES = 4 * 1024 * 1024,
    KERNEL_HEAP_MAX_BLOCKS = 1024
};

enum kernel_heap_owner {
    KERNEL_HEAP_OWNER_FREE = 0,
    KERNEL_HEAP_OWNER_KERNEL = 1,
    KERNEL_HEAP_OWNER_SESSION = 2,
    KERNEL_HEAP_OWNER_FIRST_WORKSPACE = 3,
    KERNEL_HEAP_OWNER_LAST_WORKSPACE = 11,
    KERNEL_HEAP_OWNER_COUNT = 12
};

enum kernel_heap_result {
    KERNEL_HEAP_OK,
    KERNEL_HEAP_INVALID,
    KERNEL_HEAP_BAD_OWNER,
    KERNEL_HEAP_BAD_SIZE,
    KERNEL_HEAP_CAPACITY,
    KERNEL_HEAP_OOM,
    KERNEL_HEAP_METADATA_EXHAUSTED,
    KERNEL_HEAP_BAD_POINTER,
    KERNEL_HEAP_DOUBLE_FREE,
    KERNEL_HEAP_WRONG_OWNER,
    KERNEL_HEAP_CORRUPT
};

/* One caller-owned external metadata record. offset_bytes and length_bytes name
 * a half-open range inside the arena; no record is stored in arena bytes.
 * previous_index/next_index form an address-ordered active list. slot_active
 * distinguishes list records from reusable capacity, while allocated selects
 * an owned block versus a free block. Only the heap implementation mutates a
 * record while its descriptor is initialized. */
struct kernel_heap_block {
    size_t offset_bytes;
    size_t length_bytes;
    size_t previous_index;
    size_t next_index;
    enum kernel_heap_owner owner;
    bool slot_active;
    bool allocated;
};

/* Caller-owned heap descriptor. arena and blocks are borrowed for the complete
 * initialized lifetime and must be disjoint from each other and this object.
 * Counts measure aligned block bytes, not the caller's original request size.
 * first_block_index is valid only while initialized; inactive list links use
 * SIZE_MAX. The descriptor is not safe for concurrent or interrupt-context use. */
struct kernel_heap {
    uint8_t *arena;
    size_t arena_bytes;
    struct kernel_heap_block *blocks;
    size_t block_capacity;
    size_t first_block_index;
    size_t active_blocks;
    size_t allocated_bytes;
    size_t free_bytes;
    size_t live_allocations;
    size_t owner_bytes[KERNEL_HEAP_OWNER_COUNT];
    size_t owner_allocations[KERNEL_HEAP_OWNER_COUNT];
    bool initialized;
};

/* Initialize a ZEROED, unused caller-owned descriptor over a caller-owned byte
 * arena and external record array. arena_bytes must be a nonzero multiple of
 * 16 no larger than 4 MiB; arena must be 16-byte aligned. block_capacity is in
 * records, must be 1..1024, and need not cover every 16-byte unit. All three
 * storage ranges must be representable and disjoint. Success creates one free
 * block spanning the arena and clears all other records/counters. Any error
 * leaves descriptor, arena and records unchanged. The function does not obtain,
 * zero or release backing pages and transfers no storage ownership. One CPU,
 * interrupts disabled; no locks, allocation, logging or hardware access. */
enum kernel_heap_result kernel_heap_initialize(struct kernel_heap *heap,
    void *arena, size_t arena_bytes, struct kernel_heap_block *blocks,
    size_t block_capacity);

/* Allocate one internal block for owner using bounded address-ordered first fit.
 * size_bytes must be 1..1 MiB and is rounded up to 16 bytes after checked
 * arithmetic. A larger free block is split only when an unused external record
 * exists; metadata exhaustion is explicit and never consumes the whole block.
 * Success clears the complete rounded block, then stores its 16-byte-aligned
 * arena pointer in *allocation. All failures preserve *allocation, counters,
 * arena bytes and every record. The returned storage remains borrowed from the
 * heap until a matching release; it must not outlive the descriptor/backing
 * storage. One CPU, interrupts disabled; allocation is forbidden in interrupt
 * context. allocation points to caller-owned output storage disjoint from the
 * descriptor, arena and record array. */
enum kernel_heap_result kernel_heap_allocate(struct kernel_heap *heap,
    enum kernel_heap_owner owner, size_t size_bytes, void **allocation);

/* Release an allocation only when allocation equals the checked start of one
 * live block and owner matches exactly. The pointer is converted to a numeric
 * offset for range/alignment checks and is NEVER dereferenced for metadata.
 * A pointer at a current free-block start reports DOUBLE_FREE; an interior,
 * foreign or stale coalesced pointer reports BAD_POINTER. Both are rejected.
 * Success updates counters, coalesces adjacent free blocks in address order and
 * returns retired external records to the unused pool. Freed payload bytes are
 * not cleared. Errors preserve all state. The allocation becomes invalid on
 * success; one CPU, interrupts disabled, no concurrent/interrupt-context use. */
enum kernel_heap_result kernel_heap_release(struct kernel_heap *heap,
    enum kernel_heap_owner owner, void *allocation);

/* Release every allocation belonging to exactly one workspace owner. The owner
 * must be WORKSPACE 1..9; kernel and session tokens are rejected so a workspace
 * reset cannot cross either lifetime. Success is also returned when that
 * workspace owns no blocks. Payload bytes are retained, all newly adjacent free
 * blocks are coalesced, and other owners/counters remain unchanged. A corrupt
 * heap or invalid owner is rejected before mutation. This operation does not
 * release arena backing frames or supervisor reserve: those remain owned by the
 * caller outside M4/M5. One boot CPU, interrupts disabled; no IRQ/concurrent use. */
enum kernel_heap_result kernel_heap_release_workspace(
    struct kernel_heap *heap, enum kernel_heap_owner workspace_owner);

/* Release every SESSION allocation as a separate lifetime operation. This API
 * takes no arbitrary owner token, so it cannot be used to reset a workspace or
 * release kernel blocks. Success is idempotent when the session owns no blocks;
 * payload is retained and adjacent free blocks are coalesced. Corruption is
 * rejected before mutation. Arena backing/reserve ownership is unchanged.
 * One boot CPU, interrupts disabled; no interrupt or concurrent use. */
enum kernel_heap_result kernel_heap_release_session(struct kernel_heap *heap);

/* Validate the complete external-metadata graph before any mutating operation:
 * bounded indices before dereference, reciprocal links, strict address order,
 * gap-free arena coverage, aligned nonzero block lengths, owner/state agreement,
 * no adjacent free blocks and independently recomputed global/per-owner counts.
 * Borrow all storage read-only and return false on any inconsistency. No arena
 * payload is read, no pointer supplied by an allocation caller is dereferenced,
 * and no state/output changes. One CPU, interrupts disabled. */
bool kernel_heap_check(const struct kernel_heap *heap);

#endif
