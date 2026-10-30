/* Purpose: define the bounded M6 retained-memory service contract.
 * Responsibilities: own one M4 heap descriptor and 64 external block records,
 * retain heap-allocated session/workspace identity roots, protect those roots,
 * and expose checked ordinary allocation, release, lookup and workspace reset.
 * Dependencies: kernel-heap.h and caller-supplied ordinary kernel backing that
 * is already accessible, zeroed and at least one 4096-byte page.
 * Invariants: workspace IDs 1..9 map to heap owners 3..11; all ten roots are
 * live exact-owner blocks before any root is dereferenced; ready state is never
 * partial; caller storage remains stable and disjoint for the service lifetime.
 * Concurrency: one CPU with interrupts disabled; no locks or IRQ-context use.
 * References: ADR-persistent-memory-service and ADR-memory-services. */
#ifndef SPIFFOS_MEMORY_SERVICE_H
#define SPIFFOS_MEMORY_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel-heap.h"

enum {
    MEMORY_SERVICE_WORKSPACE_COUNT = 9,
    MEMORY_SERVICE_BLOCK_CAPACITY = 64,
    MEMORY_SERVICE_MIN_ARENA_BYTES = 4096
};

/* Every public operation reports a service-level result. Heap failures remain
 * distinguishable so an internal caller can tell ordinary capacity pressure
 * from invalid ownership, a protected root, or state that must be abandoned. */
enum memory_service_result {
    MEMORY_SERVICE_OK,
    MEMORY_SERVICE_INVALID,
    MEMORY_SERVICE_NOT_READY,
    MEMORY_SERVICE_RETIRED,
    MEMORY_SERVICE_BAD_OWNER,
    MEMORY_SERVICE_BAD_SIZE,
    MEMORY_SERVICE_CAPACITY,
    MEMORY_SERVICE_OOM,
    MEMORY_SERVICE_METADATA_EXHAUSTED,
    MEMORY_SERVICE_BAD_POINTER,
    MEMORY_SERVICE_DOUBLE_FREE,
    MEMORY_SERVICE_WRONG_OWNER,
    MEMORY_SERVICE_ROOT_PROTECTED,
    MEMORY_SERVICE_BAD_WORKSPACE_ID,
    MEMORY_SERVICE_GENERATION_OVERFLOW,
    MEMORY_SERVICE_CORRUPT
};

/* Heap-allocated session identity. workspace_count is fixed at nine in M6 and
 * active_workspace_id starts at one. M6 exposes no workspace-switch mutation;
 * callers receive only borrowed const pointers. */
struct memory_service_session {
    size_t workspace_count;
    size_t active_workspace_id;
};

/* Heap-allocated workspace lifetime identity. workspace_id is immutable and
 * reset_generation starts at one, then increases after each successful reset.
 * The generation records lifetime but does not make stale raw pointers safe. */
struct memory_service_workspace {
    size_t workspace_id;
    size_t reset_generation;
};

/* Caller-owned service descriptor. heap metadata is embedded here but remains
 * external to arena payload bytes. root_bytes/root_allocations explicitly
 * account for service roots by heap owner; they are not workspace quotas or
 * implicit per-workspace partitions. A retired service contains no descriptor
 * or root pointer that a caller could accidentally use after releasing backing.
 * Fields are public only so static kernel storage can be declared without a
 * hidden allocator; callers must use the functions below to inspect state. */
struct memory_service {
    struct kernel_heap heap;
    struct kernel_heap_block blocks[MEMORY_SERVICE_BLOCK_CAPACITY];
    struct memory_service_session *session_root;
    struct memory_service_workspace
        *workspace_roots[MEMORY_SERVICE_WORKSPACE_COUNT];
    size_t root_bytes[KERNEL_HEAP_OWNER_COUNT];
    size_t root_allocations[KERNEL_HEAP_OWNER_COUNT];
    bool ready;
    bool retired;
};

/* Initialize a ZEROED, unused service over caller-owned arena bytes. arena must
 * be 16-byte aligned, disjoint from the ENTIRE service, a multiple of 16, at
 * least 4096 bytes and within existing M4 bounds. Success creates one session
 * and nine workspace roots, validates their live exact-owner blocks and fields,
 * then publishes ready last. Pre-mutation validation failures preserve every
 * byte. A failure after heap mutation retires all descriptor/root pointers;
 * arena bytes may have changed because no service was published. The caller
 * retains arena/service ownership and must keep both stable until retirement or
 * shutdown. One CPU, interrupts disabled; no locks, logging or hardware access. */
enum memory_service_result memory_service_initialize(
    struct memory_service *service, void *arena, size_t arena_bytes);

/* Validate the complete heap and all service accounting/root blocks before
 * reading any root payload. Root blocks must have their exact expected rounded
 * size and owner, IDs must match owners, and session fields must name nine
 * workspaces with active ID one. Pure/read-only: corruption is reported without
 * repair or mutation. NOT_READY and RETIRED are distinct. Caller retains all
 * storage; one CPU with interrupts disabled and no concurrent mutation. */
enum memory_service_result memory_service_check(
    const struct memory_service *service);

/* Borrow the const session root through caller-owned output storage. On success
 * *session receives a pointer valid until service retirement. The output object
 * must be disjoint from the whole service and arena so publication cannot alter
 * metadata or payload. Every failure preserves *session and all service bytes.
 * One CPU, interrupts disabled; no concurrent reset/retirement. */
enum memory_service_result memory_service_get_session(
    const struct memory_service *service,
    const struct memory_service_session **session);

/* Borrow one const workspace root by ID 1..9. The returned pointer is valid only
 * until that workspace resets or the service retires. Output alias validation
 * covers the whole service and arena. Failure preserves *workspace and storage.
 * One CPU, interrupts disabled; no concurrent reset or interrupt-context use. */
enum memory_service_result memory_service_get_workspace(
    const struct memory_service *service, size_t workspace_id,
    const struct memory_service_workspace **workspace);

/* Allocate size_bytes for one ordinary M4 owner: kernel, session, or workspace
 * 1..9. FREE and every other token are rejected; no reserve fallback exists.
 * The heap rounds to 16 bytes and clears the complete rounded block before
 * publishing it. allocation must be disjoint from the whole service and arena.
 * Normal failures preserve *allocation, heap records, counters and arena bytes.
 * Returned storage is borrowed until matching release/reset/retirement. One CPU,
 * interrupts disabled; allocation is forbidden in interrupt context. */
enum memory_service_result memory_service_allocate(
    struct memory_service *service, enum kernel_heap_owner owner,
    size_t size_bytes, void **allocation);

/* Release one exact block-start pointer using its exact ordinary owner. All ten
 * service roots are protected regardless of their owner and cannot be released
 * through this API. The supplied pointer is classified numerically and is never
 * dereferenced. Normal errors preserve service state and payload. Success may
 * coalesce metadata and invalidates allocation. One CPU, interrupts disabled;
 * no concurrent or interrupt-context use. */
enum memory_service_result memory_service_release(
    struct memory_service *service, enum kernel_heap_owner owner,
    void *allocation);

/* Reset exactly workspace_id 1..9. Complete invariants and generation overflow
 * are checked before mutation. Success releases only that workspace owner's
 * root/client blocks, reconstructs its same-ID root with generation incremented,
 * and preserves every other owner. Root-first arena placement guarantees an
 * exact root-sized first free block, or a coalesce that frees a metadata record,
 * so reconstruction cannot fail under valid metadata pressure. Any unexpected
 * post-release failure retires every descriptor/root pointer before returning.
 * One CPU, interrupts disabled; raw pre-reset workspace pointers become stale. */
enum memory_service_result memory_service_reset_workspace(
    struct memory_service *service, size_t workspace_id);

#endif
