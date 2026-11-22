/* Purpose: bounded D5.9-B diagnostic over the real memory service/heap.
 * Dependencies: same CPU adapter/D5.7 guard, M6 service and M4 heap APIs.
 * Invariants: no second allocator; 4096-byte arena, 64 records, 32-byte client;
 * no returned pointer, IRQ allocation, reset, log, callback or force-unlock.
 * References: ADR-m10-d59b-service-transactions.md, AGENTS.md.
 * Provenance: Qwen tiny service draft, coordinator review/corrections. */
#include "m10-d59b-service.h"

enum { D59B_SERVICE_WORKERS=2, D59B_SERVICE_CLIENT_BYTES=32 };

/* Borrow a live service for one synchronous transaction. Check zeroed payload,
 * denied ownership/root operations, recoverable OOM and complete restoration.
 * All payload access stays within guard lifetime. Unexpected failure latches
 * D5.7 fault and retains IF=0; it never guesses recovery or releases a root. */
bool d59b_service_probe(struct d59b_context *context,
    struct memory_service *service,unsigned index) {
    if (index >= D59B_SERVICE_WORKERS) return false;

    const enum kernel_heap_owner owner=(enum kernel_heap_owner)(KERNEL_HEAP_OWNER_FIRST_WORKSPACE+index);
    const enum kernel_heap_owner other_owner=(enum kernel_heap_owner)(KERNEL_HEAP_OWNER_FIRST_WORKSPACE+D59B_SERVICE_WORKERS-1u-index);
    uint32_t cookie;
    if (!d59b_enter(context, &cookie)) return false;

    /* Enforce the diagnostic size bound BEFORE traversing any metadata.
     * The general heap can be much larger and is not admitted by this probe. */
    if (service == NULL || service->heap.arena_bytes != MEMORY_SERVICE_MIN_ARENA_BYTES ||
        service->heap.block_capacity != MEMORY_SERVICE_BLOCK_CAPACITY ||
        memory_service_check(service) != MEMORY_SERVICE_OK) {
        context->guard->faulted = true;
        return false;
    }

    size_t free_before = service->heap.free_bytes;
    size_t alloc_before = service->heap.allocated_bytes;
    size_t live_before = service->heap.live_allocations;

    void *ptr = NULL;
    enum memory_service_result result = memory_service_allocate(
        service,
        owner,
        D59B_SERVICE_CLIENT_BYTES,
        &ptr
    );

    if (result != MEMORY_SERVICE_OK || !ptr) {
        context->guard->faulted = true;
        return false;
    }

    /* M6 published this live exact-owner block only after zeroing. Borrow
     * its byte payload solely inside the current D5.7 guard lifetime. */
    uint8_t *bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < D59B_SERVICE_CLIENT_BYTES; ++i) {
        if (bytes[i] != 0) {
            context->guard->faulted = true;
            return false;
        }
    }

    for (size_t i=0;i<D59B_SERVICE_CLIENT_BYTES;++i) bytes[i]=(uint8_t)(index+1u);

    result = memory_service_release(
        service,
        other_owner,
        ptr
    );

    if (result != MEMORY_SERVICE_WRONG_OWNER) {
        context->guard->faulted = true;
        return false;
    }

    for(size_t i=0;i<D59B_SERVICE_CLIENT_BYTES;++i) if (bytes[i] != (uint8_t)(index + 1)) {
        context->guard->faulted = true;
        return false;
    }

    /* Deliberately invalid numeric marker is NEVER dereferenced. An OOM
     * result must preserve the caller's output, rather than publish NULL. */
    void *oom_ptr = (void *)(uintptr_t)UINT64_C(0x12340);
    result = memory_service_allocate(
        service,
        owner,
        service->heap.arena_bytes,
        &oom_ptr
    );

    if (result != MEMORY_SERVICE_OOM || oom_ptr != (void *)(uintptr_t)UINT64_C(0x12340)) {
        context->guard->faulted = true;
        return false;
    }

    result = memory_service_release(
        service,
        owner,
        (void*)service->session_root
    );

    if (result != MEMORY_SERVICE_ROOT_PROTECTED) {
        context->guard->faulted = true;
        return false;
    }

    result = memory_service_release(
        service,
        owner,
        ptr
    );

    if (result != MEMORY_SERVICE_OK) {
        context->guard->faulted = true;
        return false;
    }

    if (service->heap.free_bytes != free_before ||
        service->heap.allocated_bytes != alloc_before ||
        service->heap.live_allocations != live_before) {
        context->guard->faulted = true;
        return false;
    }

    if (memory_service_check(service) != MEMORY_SERVICE_OK) {
        context->guard->faulted = true;
        return false;
    }

    return d59b_exit(context, cookie);
}
