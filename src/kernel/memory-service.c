/* Purpose: implement the pure M6 retained-memory service over the M4/M5 heap.
 * Responsibilities: initialize and validate ten protected state roots, expose
 * ordinary owner allocation/release and const lookup, and reset one workspace
 * without disturbing other owners.
 * Dependencies: memory-service.h and the unchanged bounded kernel_heap API;
 * the caller supplies M3-validated ordinary backing and owns all storage.
 * Invariants: roots occupy the first ten heap blocks in session/workspace order;
 * no root payload is read before its complete heap, exact owner and size pass;
 * ready state is all-or-nothing and retirement clears every backing pointer.
 * Concurrency: one CPU with interrupts disabled; no locks or IRQ-context use.
 * References: ADR-persistent-memory-service and ADR-memory-services. */
#include <stddef.h>
#include <stdint.h>
#include "memory-service.h"

/* Build a nonempty half-open numeric range without an overflowing addition.
 * Numeric comparison avoids undefined relational comparisons between unrelated
 * C objects. The helper neither dereferences nor takes ownership of storage. */
static bool memory_service_range(uintptr_t start, size_t length, uintptr_t *end)
{
    if (length == 0 || length > UINTPTR_MAX - start) {
        return false;
    }
    *end = start + length;
    return true;
}

/* Compare two already validated half-open numeric ranges. Adjacent ranges are
 * allowed. This pure helper performs no pointer arithmetic or memory access. */
static bool memory_service_ranges_overlap(uintptr_t first_start,
    uintptr_t first_end, uintptr_t second_start, uintptr_t second_end)
{
    return first_start < second_end && second_start < first_end;
}

/* Validate M6 arena bounds/alignment and disjointness from the ENTIRE service,
 * not merely the embedded heap descriptor and records. Pure numeric checks run
 * before initialization mutates service, records or arena bytes. */
static bool memory_service_storage_valid(const struct memory_service *service,
    const void *arena, size_t arena_bytes)
{
    if (service == NULL || arena == NULL ||
        arena_bytes < MEMORY_SERVICE_MIN_ARENA_BYTES ||
        arena_bytes > KERNEL_HEAP_MAX_ARENA_BYTES ||
        arena_bytes % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        (uintptr_t)arena % KERNEL_HEAP_ALIGNMENT_BYTES != 0) {
        return false;
    }

    uintptr_t service_start = (uintptr_t)service;
    uintptr_t arena_start = (uintptr_t)arena;
    uintptr_t service_end;
    uintptr_t arena_end;
    return memory_service_range(service_start, sizeof *service, &service_end) &&
        memory_service_range(arena_start, arena_bytes, &arena_end) &&
        !memory_service_ranges_overlap(service_start, service_end,
                                       arena_start, arena_end);
}

/* Require the documented zeroed, unused state without reading structure
 * padding. Initialization is intentionally not a reinitialization operation;
 * a retired descriptor stays visibly retired after its backing is returned. */
static bool memory_service_is_zeroed(const struct memory_service *service)
{
    if (service->heap.arena != NULL || service->heap.arena_bytes != 0 ||
        service->heap.blocks != NULL || service->heap.block_capacity != 0 ||
        service->heap.first_block_index != 0 ||
        service->heap.active_blocks != 0 ||
        service->heap.allocated_bytes != 0 || service->heap.free_bytes != 0 ||
        service->heap.live_allocations != 0 || service->heap.initialized ||
        service->session_root != NULL || service->ready || service->retired) {
        return false;
    }
    for (size_t owner = 0; owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        if (service->heap.owner_bytes[owner] != 0 ||
            service->heap.owner_allocations[owner] != 0 ||
            service->root_bytes[owner] != 0 ||
            service->root_allocations[owner] != 0) {
            return false;
        }
    }
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        if (service->workspace_roots[index] != NULL) {
            return false;
        }
    }
    for (size_t index = 0; index < MEMORY_SERVICE_BLOCK_CAPACITY; ++index) {
        const struct kernel_heap_block *block = &service->blocks[index];
        if (block->offset_bytes != 0 || block->length_bytes != 0 ||
            block->previous_index != 0 || block->next_index != 0 ||
            block->owner != KERNEL_HEAP_OWNER_FREE || block->slot_active ||
            block->allocated) {
            return false;
        }
    }
    return true;
}

/* Convert one workspace ID to the fixed M4 owner token. The caller validates
 * the 1..9 range first, so the addition is bounded by the enum's accepted range. */
static enum kernel_heap_owner memory_service_workspace_owner(
    size_t workspace_id)
{
    return (enum kernel_heap_owner)(KERNEL_HEAP_OWNER_FIRST_WORKSPACE +
                                    workspace_id - 1);
}

/* Round an internal root size using the heap's public alignment rule. Root
 * structures are compile-time small, but the checked form keeps this validation
 * independent of an ABI's size_t width. False means the contract cannot fit. */
static bool memory_service_rounded_size(size_t size_bytes,
    size_t *rounded_bytes)
{
    if (size_bytes == 0 ||
        size_bytes > KERNEL_HEAP_MAX_ALLOCATION_BYTES ||
        size_bytes > SIZE_MAX - (KERNEL_HEAP_ALIGNMENT_BYTES - 1)) {
        return false;
    }
    *rounded_bytes =
        (size_bytes + (KERNEL_HEAP_ALIGNMENT_BYTES - 1)) &
        ~(size_t)(KERNEL_HEAP_ALIGNMENT_BYTES - 1);
    return true;
}

/* Translate an unchanged heap result into the service's explicit vocabulary.
 * No state is inspected or changed. */
static enum memory_service_result memory_service_heap_result(
    enum kernel_heap_result result)
{
    switch (result) {
    case KERNEL_HEAP_OK:
        return MEMORY_SERVICE_OK;
    case KERNEL_HEAP_INVALID:
        return MEMORY_SERVICE_INVALID;
    case KERNEL_HEAP_BAD_OWNER:
        return MEMORY_SERVICE_BAD_OWNER;
    case KERNEL_HEAP_BAD_SIZE:
        return MEMORY_SERVICE_BAD_SIZE;
    case KERNEL_HEAP_CAPACITY:
        return MEMORY_SERVICE_CAPACITY;
    case KERNEL_HEAP_OOM:
        return MEMORY_SERVICE_OOM;
    case KERNEL_HEAP_METADATA_EXHAUSTED:
        return MEMORY_SERVICE_METADATA_EXHAUSTED;
    case KERNEL_HEAP_BAD_POINTER:
        return MEMORY_SERVICE_BAD_POINTER;
    case KERNEL_HEAP_DOUBLE_FREE:
        return MEMORY_SERVICE_DOUBLE_FREE;
    case KERNEL_HEAP_WRONG_OWNER:
        return MEMORY_SERVICE_WRONG_OWNER;
    case KERNEL_HEAP_CORRUPT:
        return MEMORY_SERVICE_CORRUPT;
    }
    return MEMORY_SERVICE_CORRUPT;
}

/* Report whether an owner is eligible for ordinary service allocations. FREE
 * is metadata, and the frame allocator's supervisor token is deliberately not
 * a heap owner. This pure check establishes array-index safety for owner. */
static bool memory_service_owner_valid(enum kernel_heap_owner owner)
{
    return owner >= KERNEL_HEAP_OWNER_KERNEL &&
        owner < KERNEL_HEAP_OWNER_COUNT;
}

/* Prove pointer names the start of one live exact-owner root block with the
 * expected rounded size. kernel_heap_check() MUST have completed first, making
 * every traversed index/range trustworthy. The root itself is never dereferenced. */
static bool memory_service_root_block_valid(
    const struct memory_service *service, const void *pointer,
    enum kernel_heap_owner owner, size_t expected_size_bytes)
{
    size_t rounded_bytes;
    if (pointer == NULL ||
        !memory_service_rounded_size(expected_size_bytes, &rounded_bytes)) {
        return false;
    }

    uintptr_t arena_start = (uintptr_t)service->heap.arena;
    uintptr_t pointer_address = (uintptr_t)pointer;
    uintptr_t arena_end;
    if (!memory_service_range(arena_start, service->heap.arena_bytes,
                              &arena_end) ||
        pointer_address < arena_start || pointer_address >= arena_end) {
        return false;
    }
    size_t pointer_offset = (size_t)(pointer_address - arena_start);
    if (pointer_offset % KERNEL_HEAP_ALIGNMENT_BYTES != 0) {
        return false;
    }

    size_t current_index = service->heap.first_block_index;
    for (size_t visited = 0; visited < service->heap.active_blocks; ++visited) {
        const struct kernel_heap_block *block =
            &service->heap.blocks[current_index];
        if (block->offset_bytes == pointer_offset) {
            return block->allocated && block->owner == owner &&
                block->length_bytes == rounded_bytes;
        }
        if (block->offset_bytes > pointer_offset) {
            return false;
        }
        current_index = block->next_index;
    }
    return false;
}

/* Validate a pointer-sized output object against the complete service object
 * and its entire arena. The service has already passed internal validation, so
 * its arena range is representable. No output byte is read or written here. */
static bool memory_service_output_valid(const struct memory_service *service,
    const void *output)
{
    if (output == NULL) {
        return false;
    }
    uintptr_t output_start = (uintptr_t)output;
    uintptr_t service_start = (uintptr_t)service;
    uintptr_t arena_start = (uintptr_t)service->heap.arena;
    uintptr_t output_end;
    uintptr_t service_end;
    uintptr_t arena_end;
    return memory_service_range(output_start, sizeof(void *), &output_end) &&
        memory_service_range(service_start, sizeof *service, &service_end) &&
        memory_service_range(arena_start, service->heap.arena_bytes,
                             &arena_end) &&
        !memory_service_ranges_overlap(output_start, output_end,
                                       service_start, service_end) &&
        !memory_service_ranges_overlap(output_start, output_end,
                                       arena_start, arena_end);
}

/* Validate staged or published state. Every root's allocation is proven before
 * the first root payload read. allow_staged permits initialization's fully built
 * but not-yet-ready candidate; public checking never uses that exception. */
static enum memory_service_result memory_service_validate(
    const struct memory_service *service, bool allow_staged)
{
    if (service == NULL) {
        return MEMORY_SERVICE_INVALID;
    }
    if (service->retired) {
        return MEMORY_SERVICE_RETIRED;
    }
    if (!allow_staged && !service->ready) {
        return MEMORY_SERVICE_NOT_READY;
    }
    if (allow_staged && service->ready) {
        return MEMORY_SERVICE_CORRUPT;
    }
    if (!service->heap.initialized ||
        service->heap.blocks != service->blocks ||
        service->heap.block_capacity != MEMORY_SERVICE_BLOCK_CAPACITY ||
        service->heap.arena_bytes < MEMORY_SERVICE_MIN_ARENA_BYTES ||
        !memory_service_storage_valid(service, service->heap.arena,
                                      service->heap.arena_bytes) ||
        !kernel_heap_check(&service->heap)) {
        return MEMORY_SERVICE_CORRUPT;
    }

    size_t session_root_bytes;
    size_t workspace_root_bytes;
    if (!memory_service_rounded_size(sizeof *service->session_root,
                                     &session_root_bytes) ||
        !memory_service_rounded_size(sizeof *service->workspace_roots[0],
                                     &workspace_root_bytes)) {
        return MEMORY_SERVICE_CORRUPT;
    }
    for (size_t owner = 0; owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        size_t expected_bytes = 0;
        size_t expected_allocations = 0;
        if (owner == KERNEL_HEAP_OWNER_SESSION) {
            expected_bytes = session_root_bytes;
            expected_allocations = 1;
        } else if (owner >= KERNEL_HEAP_OWNER_FIRST_WORKSPACE &&
                   owner <= KERNEL_HEAP_OWNER_LAST_WORKSPACE) {
            expected_bytes = workspace_root_bytes;
            expected_allocations = 1;
        }
        if (service->root_bytes[owner] != expected_bytes ||
            service->root_allocations[owner] != expected_allocations ||
            service->heap.owner_bytes[owner] < expected_bytes ||
            service->heap.owner_allocations[owner] < expected_allocations) {
            return MEMORY_SERVICE_CORRUPT;
        }
    }

    if (!memory_service_root_block_valid(service, service->session_root,
                                         KERNEL_HEAP_OWNER_SESSION,
                                         sizeof *service->session_root)) {
        return MEMORY_SERVICE_CORRUPT;
    }
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        enum kernel_heap_owner owner =
            memory_service_workspace_owner(index + 1);
        if (!memory_service_root_block_valid(
                service, service->workspace_roots[index], owner,
                sizeof *service->workspace_roots[index])) {
            return MEMORY_SERVICE_CORRUPT;
        }
    }

    /* Only now is it safe to read root payloads: the complete heap and every
     * root pointer, exact owner and exact allocation size passed above. */
    if (service->session_root->workspace_count !=
            MEMORY_SERVICE_WORKSPACE_COUNT ||
         (service->session_root->active_workspace_id < 1 ||
         service->session_root->active_workspace_id > MEMORY_SERVICE_WORKSPACE_COUNT)) {
        return MEMORY_SERVICE_CORRUPT;
    }
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        if (service->workspace_roots[index]->workspace_id != index + 1 ||
            service->workspace_roots[index]->reset_generation == 0) {
            return MEMORY_SERVICE_CORRUPT;
        }
    }
    return MEMORY_SERVICE_OK;
}

/* Retire a service after initialization mutation or an unexpected reset-path
 * failure. Explicit field stores avoid a giant aggregate clear that a compiler
 * might lower to an unavailable freestanding memset(). Root and backing pointers
 * are cleared before retired is published. Caller still owns/reclaims backing. */
void memory_service_retire(struct memory_service *service)
{
    if (service == NULL) return;
    service->session_root = NULL;
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        service->workspace_roots[index] = NULL;
    }
    service->heap.arena = NULL;
    service->heap.arena_bytes = 0;
    service->heap.blocks = NULL;
    service->heap.block_capacity = 0;
    service->heap.first_block_index = 0;
    service->heap.active_blocks = 0;
    service->heap.allocated_bytes = 0;
    service->heap.free_bytes = 0;
    service->heap.live_allocations = 0;
    for (size_t owner = 0; owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        service->heap.owner_bytes[owner] = 0;
        service->heap.owner_allocations[owner] = 0;
        service->root_bytes[owner] = 0;
        service->root_allocations[owner] = 0;
    }
    service->heap.initialized = false;
    for (size_t index = 0; index < MEMORY_SERVICE_BLOCK_CAPACITY; ++index) {
        service->blocks[index].offset_bytes = 0;
        service->blocks[index].length_bytes = 0;
        service->blocks[index].previous_index = 0;
        service->blocks[index].next_index = 0;
        service->blocks[index].owner = KERNEL_HEAP_OWNER_FREE;
        service->blocks[index].slot_active = false;
        service->blocks[index].allocated = false;
    }
    service->ready = false;
    service->retired = true;
}

/* Initialize all roots in deterministic session/workspace order. Root blocks
 * therefore remain before every client allocation, a property used by reset's
 * metadata-pressure reconstruction proof. See the header for ownership/failure
 * and concurrency contracts. */
enum memory_service_result memory_service_initialize(
    struct memory_service *service, void *arena, size_t arena_bytes)
{
    if (!memory_service_storage_valid(service, arena, arena_bytes) ||
        !memory_service_is_zeroed(service)) {
        return MEMORY_SERVICE_INVALID;
    }

    enum kernel_heap_result heap_result = kernel_heap_initialize(
        &service->heap, arena, arena_bytes, service->blocks,
        MEMORY_SERVICE_BLOCK_CAPACITY);
    if (heap_result != KERNEL_HEAP_OK) {
        return memory_service_heap_result(heap_result);
    }

    void *session_pointer = NULL;
    heap_result = kernel_heap_allocate(&service->heap,
        KERNEL_HEAP_OWNER_SESSION, sizeof(struct memory_service_session),
        &session_pointer);
    if (heap_result != KERNEL_HEAP_OK ||
        !memory_service_root_block_valid(service, session_pointer,
            KERNEL_HEAP_OWNER_SESSION,
            sizeof(struct memory_service_session))) {
        enum memory_service_result result = heap_result == KERNEL_HEAP_OK ?
            MEMORY_SERVICE_CORRUPT : memory_service_heap_result(heap_result);
        memory_service_retire(service);
        return result;
    }
    struct memory_service_session *session_root = session_pointer;
    session_root->workspace_count = MEMORY_SERVICE_WORKSPACE_COUNT;
    session_root->active_workspace_id = 1;

    struct memory_service_workspace *workspace_roots[
        MEMORY_SERVICE_WORKSPACE_COUNT];
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        workspace_roots[index] = NULL;
    }
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        enum kernel_heap_owner owner =
            memory_service_workspace_owner(index + 1);
        void *workspace_pointer = NULL;
        heap_result = kernel_heap_allocate(&service->heap, owner,
            sizeof(struct memory_service_workspace), &workspace_pointer);
        if (heap_result != KERNEL_HEAP_OK ||
            !memory_service_root_block_valid(service, workspace_pointer, owner,
                sizeof(struct memory_service_workspace))) {
            enum memory_service_result result =
                heap_result == KERNEL_HEAP_OK ? MEMORY_SERVICE_CORRUPT :
                memory_service_heap_result(heap_result);
            memory_service_retire(service);
            return result;
        }
        workspace_roots[index] = workspace_pointer;
        workspace_roots[index]->workspace_id = index + 1;
        workspace_roots[index]->reset_generation = 1;
    }

    /* Roots are staged while ready remains false. Public getters cannot observe
     * partial state. Explicit stores avoid aggregate copies/runtime helpers. */
    service->session_root = session_root;
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        service->workspace_roots[index] = workspace_roots[index];
    }
    size_t session_root_bytes;
    size_t workspace_root_bytes;
    if (!memory_service_rounded_size(sizeof(struct memory_service_session),
                                     &session_root_bytes) ||
        !memory_service_rounded_size(sizeof(struct memory_service_workspace),
                                     &workspace_root_bytes)) {
        memory_service_retire(service);
        return MEMORY_SERVICE_CORRUPT;
    }
    service->root_bytes[KERNEL_HEAP_OWNER_SESSION] = session_root_bytes;
    service->root_allocations[KERNEL_HEAP_OWNER_SESSION] = 1;
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        enum kernel_heap_owner owner =
            memory_service_workspace_owner(index + 1);
        service->root_bytes[owner] = workspace_root_bytes;
        service->root_allocations[owner] = 1;
    }

    if (memory_service_validate(service, true) != MEMORY_SERVICE_OK) {
        memory_service_retire(service);
        return MEMORY_SERVICE_CORRUPT;
    }
    service->ready = true;
    return MEMORY_SERVICE_OK;
}

/* Public read-only validation; see the header for exact checked invariants. */
enum memory_service_result memory_service_check(
    const struct memory_service *service)
{
    return memory_service_validate(service, false);
}

/* Select a valid active workspace after validating all heap/root invariants.
 * The caller must prepare its editor and display before requesting the switch.
 * Failure is read-only; this API never allocates or renders. */
enum memory_service_result memory_service_select_workspace(
    struct memory_service *service, size_t workspace_id)
{
    enum memory_service_result result = memory_service_check(service);
    if (result != MEMORY_SERVICE_OK) return result;
    if (workspace_id < 1 || workspace_id > MEMORY_SERVICE_WORKSPACE_COUNT)
        return MEMORY_SERVICE_BAD_WORKSPACE_ID;
    service->session_root->active_workspace_id = workspace_id;
    return MEMORY_SERVICE_OK;
}

/* Publish the borrowed const session root only after complete state and output
 * alias validation. Failure never changes caller output or service storage. */
enum memory_service_result memory_service_get_session(
    const struct memory_service *service,
    const struct memory_service_session **session)
{
    enum memory_service_result result = memory_service_check(service);
    if (result != MEMORY_SERVICE_OK) {
        return result;
    }
    if (!memory_service_output_valid(service, session)) {
        return MEMORY_SERVICE_INVALID;
    }
    *session = service->session_root;
    return MEMORY_SERVICE_OK;
}

/* Publish one borrowed const workspace root after ID, complete service and
 * whole-storage output checks. Failure preserves the caller's pointer value. */
enum memory_service_result memory_service_get_workspace(
    const struct memory_service *service, size_t workspace_id,
    const struct memory_service_workspace **workspace)
{
    if (workspace_id == 0 ||
        workspace_id > MEMORY_SERVICE_WORKSPACE_COUNT) {
        return MEMORY_SERVICE_BAD_WORKSPACE_ID;
    }
    enum memory_service_result result = memory_service_check(service);
    if (result != MEMORY_SERVICE_OK) {
        return result;
    }
    if (!memory_service_output_valid(service, workspace)) {
        return MEMORY_SERVICE_INVALID;
    }
    *workspace = service->workspace_roots[workspace_id - 1];
    return MEMORY_SERVICE_OK;
}

/* Allocate one zero-before-publish ordinary block. Service validation precedes
 * every mutation and output alias checks cover more storage than M4 alone. */
enum memory_service_result memory_service_allocate(
    struct memory_service *service, enum kernel_heap_owner owner,
    size_t size_bytes, void **allocation)
{
    enum memory_service_result result = memory_service_check(service);
    if (result != MEMORY_SERVICE_OK) {
        return result;
    }
    if (!memory_service_owner_valid(owner)) {
        return MEMORY_SERVICE_BAD_OWNER;
    }
    if (!memory_service_output_valid(service, allocation)) {
        return MEMORY_SERVICE_INVALID;
    }
    return memory_service_heap_result(kernel_heap_allocate(
        &service->heap, owner, size_bytes, allocation));
}

/* Return true only for one of the ten already validated root pointer values.
 * Numeric equality does not dereference allocation or any root payload. */
static bool memory_service_is_root(const struct memory_service *service,
    const void *allocation)
{
    if (allocation == service->session_root) {
        return true;
    }
    for (size_t index = 0; index < MEMORY_SERVICE_WORKSPACE_COUNT; ++index) {
        if (allocation == service->workspace_roots[index]) {
            return true;
        }
    }
    return false;
}

/* Release one non-root ordinary allocation only after full invariant checking.
 * M4 performs exact-start and exact-owner classification without dereferencing
 * the supplied payload pointer. */
enum memory_service_result memory_service_release(
    struct memory_service *service, enum kernel_heap_owner owner,
    void *allocation)
{
    enum memory_service_result result = memory_service_check(service);
    if (result != MEMORY_SERVICE_OK) {
        return result;
    }
    if (!memory_service_owner_valid(owner)) {
        return MEMORY_SERVICE_BAD_OWNER;
    }
    if (memory_service_is_root(service, allocation)) {
        return MEMORY_SERVICE_ROOT_PROTECTED;
    }
    return memory_service_heap_result(kernel_heap_release(
        &service->heap, owner, allocation));
}

/* Reset one workspace lifetime. Root/client cleanup is the sole intended M5
 * owner-wide mutation here. Under valid state, reconstruction must succeed:
 * earlier roots occupy all blocks before this root; releasing an exact 16-byte
 * root leaves an exact first free fit, while any coalescing that enlarges it also
 * retires a record, supplying the split metadata. Unexpected failure retires
 * every pointer rather than returning a dangling service. */
enum memory_service_result memory_service_reset_workspace(
    struct memory_service *service, size_t workspace_id)
{
    if (workspace_id == 0 ||
        workspace_id > MEMORY_SERVICE_WORKSPACE_COUNT) {
        return MEMORY_SERVICE_BAD_WORKSPACE_ID;
    }
    enum memory_service_result result = memory_service_check(service);
    if (result != MEMORY_SERVICE_OK) {
        return result;
    }

    size_t index = workspace_id - 1;
    struct memory_service_workspace *old_root =
        service->workspace_roots[index];
    /* memory_service_check() proved old_root before this first dereference. */
    if (old_root->reset_generation == SIZE_MAX) {
        return MEMORY_SERVICE_GENERATION_OVERFLOW;
    }
    size_t next_generation = old_root->reset_generation + 1;
    enum kernel_heap_owner owner =
        memory_service_workspace_owner(workspace_id);

    enum kernel_heap_result heap_result =
        kernel_heap_release_workspace(&service->heap, owner);
    if (heap_result != KERNEL_HEAP_OK) {
        memory_service_retire(service);
        return MEMORY_SERVICE_CORRUPT;
    }
    service->workspace_roots[index] = NULL;

    void *new_pointer = NULL;
    heap_result = kernel_heap_allocate(&service->heap, owner,
        sizeof(struct memory_service_workspace), &new_pointer);
    if (heap_result != KERNEL_HEAP_OK ||
        !memory_service_root_block_valid(service, new_pointer, owner,
            sizeof(struct memory_service_workspace))) {
        memory_service_retire(service);
        return MEMORY_SERVICE_CORRUPT;
    }
    struct memory_service_workspace *new_root = new_pointer;
    new_root->workspace_id = workspace_id;
    new_root->reset_generation = next_generation;
    service->workspace_roots[index] = new_root;

    if (memory_service_check(service) != MEMORY_SERVICE_OK) {
        memory_service_retire(service);
        return MEMORY_SERVICE_CORRUPT;
    }
    return MEMORY_SERVICE_OK;
}
