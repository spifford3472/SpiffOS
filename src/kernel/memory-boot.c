/* Purpose: adapt validated Limine memory metadata to M1-M5 boot diagnostics.
 * Responsibilities: inventory candidate RAM, exercise M2 ownership, validate
 * the HHDM/paging mode, touch only owned M3 pages, and probe M4/M5 lifetimes.
 * Dependencies: pinned limine.h, M1/M2/M3/M4/M5 policy and a serial callback.
 * Invariants: static bounded metadata; one startup CPU, interrupts disabled;
 * unsupported M3 responses never change mappings, frames or editor pixels.
 * References: ADR-memory-foundation.md, ADR-frame-allocator.md,
 * ADR-memory-services.md and ADR-011. */
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "memory-map.h"
#include "memory-boot.h"
#include "frame-allocator.h"
#include "page-access.h"
#include "kernel-heap.h"
#include "memory-service.h"

_Static_assert(MEMORY_REGION_USABLE == LIMINE_MEMMAP_USABLE,
    "inventory usable type must match the pinned Limine header");

/* Only metadata is copied here. A 12KiB snapshot belongs in BSS rather than
 * Limine's entry stack; borrowed response pointers are never modified. */
static struct memory_region snapshot[MEMORY_MAP_MAX_ENTRIES];
static size_t snapshot_count;
enum { BOOT_FRAME_CAPACITY = 4 * 1024 * 1024, BOOT_RESERVE_FRAMES = 64 };
/* One byte per candidate frame retains the original 16GiB diagnostic
 * capacity while halving preboot metadata BSS. Every current owner token
 * fits in a byte; failure remains explicit if a future map exceeds capacity.
 * Reference: ADR-m9-128m-boot-memory.md. */
static uint8_t frame_owners[BOOT_FRAME_CAPACITY];
static struct frame_allocator boot_allocator;
/* True only after the complete M2 probe restored its pools and emitted its
 * success marker. M3 must remain unavailable when M2 capacity was unsupported. */
static bool boot_allocator_available;
enum { BOOT_HEAP_BLOCK_CAPACITY = 8 };
/* Heap records are external to the one-page arena by M4 contract. Keeping this
 * bounded diagnostic array in kernel BSS also avoids consuming Limine's entry
 * stack or placing allocator metadata in the frame being tested. */
static struct kernel_heap_block boot_heap_blocks[BOOT_HEAP_BLOCK_CAPACITY];
/* The probe runs once per boot. A static descriptor starts completely zeroed as
 * required by kernel_heap_initialize(), without asking the freestanding linker
 * for a hosted memset implementation or consuming additional entry-stack space. */
static struct kernel_heap boot_heap;

/* Convert any uint64_t to at most 20 decimal digits plus NUL. Stack bytes remain
 * live during the borrowed synchronous writer. False propagates partial output;
 * no printf, heap, ownership transfer or hardware access in this helper. */
static bool write_number(bool (*write_text)(const char *), uint64_t number)
{
    char digits[21];
    size_t first = sizeof digits - 1;
    digits[first] = '\0';
    do {
        digits[--first] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);
    return write_text(&digits[first]);
}

/* Emit three counted region fields with explicit separators, preserving exact
 * base/length/type for independent guest-test recomputation. False stops at
 * the first writer failure and cannot emit a later success marker. */
static bool write_region(bool (*write_text)(const char *),
    const struct memory_region *region)
{
    return write_text("MEMORY_REGION:") &&
        write_number(write_text, region->base) && write_text(",") &&
        write_number(write_text, region->length) && write_text(",") &&
        write_number(write_text, region->type) && write_text("\r\n");
}

/* Validate borrowed pointers/count before copying metadata, then validate every
 * interval before publishing entry/counter diagnostics. UNAVAILABLE allows
 * the existing editor boot path to continue without claiming usable capacity. */
bool memory_boot_report(const struct limine_memmap_response *response,
    bool (*write_text)(const char *text))
{
    snapshot_count = 0; /* Never expose an earlier map after a failed report. */
    if (write_text == NULL) {
        return false;
    }
    if (response == NULL || response->revision != 0 ||
        response->entry_count == 0 ||
        response->entry_count > MEMORY_MAP_MAX_ENTRIES ||
        response->entries == NULL) {
        return write_text("SPIFFOS_MEMORY_UNAVAILABLE\r\n");
    }
    size_t count = (size_t)response->entry_count;
    for (size_t index = 0; index < count; ++index) {
        const struct limine_memmap_entry *entry = response->entries[index];
        if (entry == NULL) {
            return write_text("SPIFFOS_MEMORY_UNAVAILABLE\r\n");
        }
        snapshot[index] = (struct memory_region){
            entry->base, entry->length, entry->type
        };
    }
    struct memory_inventory inventory;
    if (memory_map_analyze(snapshot, count, &inventory) != MEMORY_MAP_OK) {
        return write_text("SPIFFOS_MEMORY_UNAVAILABLE\r\n");
    }
    for (size_t index = 0; index < count; ++index) {
        if (!write_region(write_text, &snapshot[index])) {
            return false;
        }
    }
    bool reported = write_text("MEMORY_SUMMARY:") &&
        write_number(write_text, inventory.usable_bytes) && write_text(",") &&
        write_number(write_text, inventory.candidate_frames) && write_text(",") &&
        write_number(write_text, inventory.candidate_bytes) && write_text(",") &&
        write_number(write_text, inventory.excluded_usable_bytes) &&
        write_text("\r\nSPIFFOS_MEMORY_OK\r\n");
    if (reported) {
        snapshot_count = count;
    }
    return reported;
}


/* Report a self-test inconsistency and fail the boot diagnostic. The caller
 * must not offer an allocator that violated ownership/counter invariants.
 * Synchronous serial only; no ownership transfer or candidate-frame writes. */
static bool allocator_failure(bool (*write_text)(const char *))
{
    (void)write_text("SPIFFOS_ALLOCATOR_ERROR\r\n");
    return false;
}

/* Initialize metadata and prove reversible allocation for every diagnostic
 * owner plus the protected reserve. Call once after a complete inventory report.
 * Returned addresses remain integers: no frame contents or mappings are touched.
 * Unsupported bounds preserve the existing boot/editor path via UNAVAILABLE. */
bool memory_boot_allocator_probe(bool (*write_text)(const char *text))
{
    boot_allocator_available = false;
    if (write_text == NULL) {
        return false;
    }
    if (snapshot_count == 0 ||
        frame_allocator_initialize(&boot_allocator, snapshot, snapshot_count,
            frame_owners, BOOT_FRAME_CAPACITY, BOOT_RESERVE_FRAMES) != FRAME_OK ||
        boot_allocator.ordinary_free < FRAME_OWNER_LAST_WORKSPACE) {
        return write_text("SPIFFOS_ALLOCATOR_UNAVAILABLE\r\n");
    }
    if (!frame_allocator_check(&boot_allocator)) {
        return allocator_failure(write_text);
    }
    uint64_t ordinary[FRAME_OWNER_LAST_WORKSPACE];
    for (unsigned int owner = FRAME_OWNER_KERNEL;
         owner <= FRAME_OWNER_LAST_WORKSPACE; ++owner) {
        if (frame_allocate(&boot_allocator, (enum frame_owner)owner, false,
                           &ordinary[owner - 1]) != FRAME_OK) {
            return allocator_failure(write_text);
        }
        for (unsigned int prior = 1; prior < owner; ++prior) {
            if (ordinary[prior - 1] == ordinary[owner - 1]) {
                return allocator_failure(write_text);
            }
        }
    }
    uint64_t reserved;
    if (frame_allocate(&boot_allocator, FRAME_OWNER_SUPERVISOR, true,
                       &reserved) != FRAME_OK) {
        return allocator_failure(write_text);
    }
    for (unsigned int index = 0; index < FRAME_OWNER_LAST_WORKSPACE; ++index) {
        if (ordinary[index] == reserved) {
            return allocator_failure(write_text);
        }
    }
    uint64_t rejected = UINT64_MAX;
    if (frame_allocate(&boot_allocator, FRAME_OWNER_KERNEL, true, &rejected) !=
            FRAME_FORBIDDEN || rejected != UINT64_MAX ||
        frame_release(&boot_allocator, FRAME_OWNER_SESSION, ordinary[0]) !=
            FRAME_WRONG_OWNER ||
        !frame_allocator_check(&boot_allocator)) {
        return allocator_failure(write_text);
    }
    for (unsigned int owner = FRAME_OWNER_KERNEL;
         owner <= FRAME_OWNER_LAST_WORKSPACE; ++owner) {
        if (frame_release(&boot_allocator, (enum frame_owner)owner,
                          ordinary[owner - 1]) != FRAME_OK) {
            return allocator_failure(write_text);
        }
    }
    if (frame_release(&boot_allocator, FRAME_OWNER_SUPERVISOR, reserved) !=
            FRAME_OK ||
        frame_release(&boot_allocator, FRAME_OWNER_KERNEL, ordinary[0]) !=
            FRAME_DOUBLE_FREE) {
        return allocator_failure(write_text);
    }
    uint64_t reused;
    if (frame_allocate(&boot_allocator, FRAME_OWNER_KERNEL, false, &reused) !=
            FRAME_OK || reused != ordinary[0] ||
        frame_release(&boot_allocator, FRAME_OWNER_KERNEL, reused) != FRAME_OK ||
        !frame_allocator_check(&boot_allocator) ||
        boot_allocator.ordinary_free !=
            boot_allocator.total_frames - boot_allocator.reserve_frames ||
        boot_allocator.reserve_free != BOOT_RESERVE_FRAMES) {
        return allocator_failure(write_text);
    }
    bool reported = write_text("FRAME_POOL:") &&
        write_number(write_text, boot_allocator.total_frames) && write_text(",") &&
        write_number(write_text, boot_allocator.ordinary_free) && write_text(",") &&
        write_number(write_text, boot_allocator.reserve_free) && write_text(",") &&
        write_number(write_text, boot_allocator.reserve_frames) &&
        write_text(",0\r\nSPIFFOS_ALLOCATOR_OK\r\n");
    if (reported) {
        boot_allocator_available = true;
    }
    return reported;
}

/* Adapt only the pinned revision-0 response shapes and explicit x86-64 paging
 * modes into policy. The mode determines canonical address width; no link
 * address or identity-map fallback is inferred. Success publishes once and
 * all failures preserve caller output. Pure borrowed metadata, no logging. */
bool memory_boot_page_policy(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging,
    const struct frame_allocator *allocator,
    struct page_access_policy *policy)
{
    if (hhdm == NULL || paging == NULL || allocator == NULL || policy == NULL ||
        hhdm->revision != 0 || paging->revision != 0) {
        return false;
    }
    uint8_t address_bits;
    if (paging->mode == LIMINE_PAGING_MODE_X86_64_4LVL) {
        address_bits = PAGE_ACCESS_X86_64_4_LEVEL_BITS;
    } else if (paging->mode == LIMINE_PAGING_MODE_X86_64_5LVL) {
        address_bits = PAGE_ACCESS_X86_64_5_LEVEL_BITS;
    } else {
        return false;
    }
    struct page_access_policy validated = {
        .allocator = allocator,
        .direct_map_offset = hhdm->offset,
        .virtual_address_bits = address_bits
    };
    *policy = validated;
    return true;
}

/* Clear exactly one authorized direct-map page with volatile byte stores. The
 * volatile qualifier ensures the freestanding compiler emits every hardware
 * memory access. The policy already proved the complete numeric range and this
 * is the only M3 kernel function that converts it to a C pointer. Context is
 * unused; no ownership transfer, logging, allocation, locks or concurrency. */
static bool kernel_page_zero(void *context, uint64_t mapped_address,
    size_t length_bytes)
{
    (void)context;
    if (length_bytes != MEMORY_PAGE_BYTES) {
        return false;
    }
    volatile uint8_t *page = (volatile uint8_t *)(uintptr_t)mapped_address;
    for (size_t index = 0; index < length_bytes; ++index) {
        page[index] = 0;
    }
    return true;
}

/* Read all bytes from one previously authorized direct-map page and compare
 * them with expected. The caller retains the live exact-owner allocation for
 * the whole call. Volatile reads observe guest RAM rather than cached C state.
 * Boot CPU only, IRQs disabled; no errors beyond a mismatching byte. */
static bool kernel_page_has_value(uint64_t mapped_address, uint8_t expected)
{
    const volatile uint8_t *page =
        (const volatile uint8_t *)(uintptr_t)mapped_address;
    for (size_t index = 0; index < MEMORY_PAGE_BYTES; ++index) {
        if (page[index] != expected) {
            return false;
        }
    }
    return true;
}

/* Fill one previously authorized direct-map page, then read it back through
 * volatile accesses. Ownership and mapping authorization remain with caller;
 * this helper never stores outside the exact 4096-byte page. */
static bool kernel_page_write_and_verify(uint64_t mapped_address, uint8_t value)
{
    volatile uint8_t *page = (volatile uint8_t *)(uintptr_t)mapped_address;
    for (size_t index = 0; index < MEMORY_PAGE_BYTES; ++index) {
        page[index] = value;
    }
    return kernel_page_has_value(mapped_address, value);
}

/* Release an owned probe page on an error path. Returning false prevents a
 * success marker if ownership or allocator counters cannot be restored. */
static bool page_probe_release(uint64_t physical_address)
{
    return frame_release(&boot_allocator, FRAME_OWNER_KERNEL,
                         physical_address) == FRAME_OK;
}

/* Report a fatal M3 self-test/rollback inconsistency. Serial failure is still
 * failure; neither case permits PAGE_ACCESS_OK. */
static bool page_probe_failure(bool (*write_text)(const char *))
{
    (void)write_text("SPIFFOS_PAGE_ACCESS_ERROR\r\n");
    return false;
}

/* Prove the bootloader direct map with a real owned frame and restore the M2
 * allocator afterward. Each failure after acquisition releases the exact
 * kernel-owned page when possible. Reallocation must return the just-released
 * first-fit frame, whose complete contents are then cleared and verified.
 * Unsupported responses are non-fatal and preserve the editor boot path. */
bool memory_boot_page_probe(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging,
    bool (*write_text)(const char *text))
{
    if (write_text == NULL) {
        return false;
    }
    struct page_access_policy policy;
    if (!boot_allocator_available ||
        !memory_boot_page_policy(hhdm, paging, &boot_allocator, &policy)) {
        return write_text("SPIFFOS_PAGE_ACCESS_UNAVAILABLE\r\n");
    }
    if (!frame_allocator_check(&boot_allocator)) {
        return page_probe_failure(write_text);
    }
    uint64_t ordinary_before = boot_allocator.ordinary_free;
    uint64_t reserve_before = boot_allocator.reserve_free;
    uint64_t physical_address = UINT64_MAX;
    if (page_access_allocate_zero(&boot_allocator, &policy, FRAME_OWNER_KERNEL,
            kernel_page_zero, NULL, &physical_address) != PAGE_ACCESS_OK) {
        return page_probe_failure(write_text);
    }

    uint64_t mapped_address;
    if (page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
            &mapped_address) != PAGE_ACCESS_OK ||
        !kernel_page_has_value(mapped_address, 0) ||
        !kernel_page_write_and_verify(mapped_address, UINT8_C(0xa5))) {
        if (!page_probe_release(physical_address)) {
            return page_probe_failure(write_text);
        }
        return page_probe_failure(write_text);
    }
    if (!page_probe_release(physical_address)) {
        return page_probe_failure(write_text);
    }

    uint64_t reallocated = UINT64_MAX;
    if (page_access_allocate_zero(&boot_allocator, &policy, FRAME_OWNER_KERNEL,
            kernel_page_zero, NULL, &reallocated) != PAGE_ACCESS_OK) {
        return page_probe_failure(write_text);
    }
    if (reallocated != physical_address ||
        page_access_resolve(&policy, FRAME_OWNER_KERNEL, reallocated,
            &mapped_address) != PAGE_ACCESS_OK ||
        !kernel_page_has_value(mapped_address, 0)) {
        if (!page_probe_release(reallocated)) {
            return page_probe_failure(write_text);
        }
        return page_probe_failure(write_text);
    }
    if (!page_probe_release(reallocated) ||
        !frame_allocator_check(&boot_allocator) ||
        boot_allocator.ordinary_free != ordinary_before ||
        boot_allocator.reserve_free != reserve_before) {
        return page_probe_failure(write_text);
    }
    return write_text("SPIFFOS_PAGE_ACCESS_OK\r\n");
}

/* Fill and read back an exact live heap block through ordinary C byte accesses.
 * The caller has already proved that bytes names a currently allocated range
 * inside the M3-authorized kernel page. No metadata, ownership or output state
 * changes; false reports the first mismatching byte. Boot CPU only, IRQs off. */
static bool heap_bytes_write_and_verify(void *bytes, size_t length, uint8_t value)
{
    uint8_t *writable = bytes;
    for (size_t index = 0; index < length; ++index) {
        writable[index] = value;
    }
    for (size_t index = 0; index < length; ++index) {
        if (writable[index] != value) {
            return false;
        }
    }
    return true;
}

/* Return whether every byte in a live heap block has the expected value.
 * Storage is borrowed read-only for this synchronous call. The caller retains
 * the allocation and exact owner; no logging, mutation or failure side effect. */
static bool heap_bytes_have_value(const void *bytes, size_t length,
    uint8_t expected)
{
    const uint8_t *readable = bytes;
    for (size_t index = 0; index < length; ++index) {
        if (readable[index] != expected) {
            return false;
        }
    }
    return true;
}

/* Defined below the failure helper because the same retirement operation is
 * also used on the successful path. It only clears the static descriptor and
 * never touches its arena or external records. */
static void heap_probe_retire_descriptor(void);

/* Release the one M3 backing frame after a failed M4 self-test and emit the
 * fatal diagnostic. A failed release is deliberately still failure and never
 * permits HEAP_OK. The callback is borrowed synchronously; no heap pointer is
 * published beyond the probe. Boot CPU only with interrupts disabled. */
static bool heap_probe_failure(bool (*write_text)(const char *),
    uint64_t physical_address, bool backing_owned)
{
    /* Once initialization succeeds, the descriptor contains a usable pointer
     * into the backing page even when a later self-test fails. Retire it before
     * returning that frame to M2 so no stale metadata can authorize access to a
     * page whose ownership has ended. This is safe before initialization too:
     * the static descriptor begins zeroed and retirement is idempotent. */
    heap_probe_retire_descriptor();
    if (backing_owned) {
        (void)frame_release(&boot_allocator, FRAME_OWNER_KERNEL,
                            physical_address);
    }
    (void)write_text("SPIFFOS_HEAP_ERROR\r\n");
    return false;
}

/* Retire the diagnostic descriptor before returning its arena frame to M2. On
 * success the heap is already empty and coalesced; on failure live block records
 * are abandoned because boot stops, but the descriptor must still lose every
 * pointer before frame ownership ends. Explicit stores avoid a hosted memset.
 * Static records are harmless stale bytes and initialization clears them before
 * any hypothetical reuse. No arena payload or external record is dereferenced. */
static void heap_probe_retire_descriptor(void)
{
    boot_heap.arena = NULL;
    boot_heap.arena_bytes = 0;
    boot_heap.blocks = NULL;
    boot_heap.block_capacity = 0;
    boot_heap.first_block_index = 0;
    boot_heap.active_blocks = 0;
    boot_heap.allocated_bytes = 0;
    boot_heap.free_bytes = 0;
    boot_heap.live_allocations = 0;
    for (size_t owner = 0; owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        boot_heap.owner_bytes[owner] = 0;
        boot_heap.owner_allocations[owner] = 0;
    }
    boot_heap.initialized = false;
}

/* Build a one-page M4 heap only over a page obtained through the validated M3
 * direct-map path. Prove 16-byte alignment, real byte write/read, independent
 * kernel/session/two-workspace ownership, wrong-owner rejection and atomic OOM.
 * Resetting workspace 1 must retain workspace 2, kernel, session and the M2
 * reserve; separate session teardown must retain workspace 2 and kernel. The
 * probe then checks free/reuse with zero-before-publication. Success releases
 * every block/backing frame, restores M2 counters, and emits both lifecycle and
 * heap markers last. Unsupported M3 responses emit
 * UNAVAILABLE without changing the existing editor path. All mappings, callback
 * and static metadata are borrowed; one boot CPU owns them with IRQs disabled. */
bool memory_boot_heap_probe(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging,
    bool (*write_text)(const char *text))
{
    if (write_text == NULL) {
        return false;
    }
    struct page_access_policy policy;
    if (!boot_allocator_available ||
        !memory_boot_page_policy(hhdm, paging, &boot_allocator, &policy)) {
        return write_text("SPIFFOS_HEAP_UNAVAILABLE\r\n");
    }
    if (!frame_allocator_check(&boot_allocator)) {
        return heap_probe_failure(write_text, 0, false);
    }

    uint64_t ordinary_before = boot_allocator.ordinary_free;
    uint64_t reserve_before = boot_allocator.reserve_free;
    uint64_t physical_address = UINT64_MAX;
    if (page_access_allocate_zero(&boot_allocator, &policy, FRAME_OWNER_KERNEL,
            kernel_page_zero, NULL, &physical_address) != PAGE_ACCESS_OK) {
        return heap_probe_failure(write_text, 0, false);
    }
    uint64_t mapped_address = 0;
    if (page_access_resolve(&policy, FRAME_OWNER_KERNEL, physical_address,
            &mapped_address) != PAGE_ACCESS_OK ||
        mapped_address % KERNEL_HEAP_ALIGNMENT_BYTES != 0) {
        return heap_probe_failure(write_text, physical_address, true);
    }

    if (kernel_heap_initialize(&boot_heap, (void *)(uintptr_t)mapped_address,
            MEMORY_PAGE_BYTES, boot_heap_blocks,
            BOOT_HEAP_BLOCK_CAPACITY) != KERNEL_HEAP_OK) {
        return heap_probe_failure(write_text, physical_address, true);
    }
    void *kernel_allocation = NULL;
    void *session_allocation = NULL;
    void *workspace1_first = NULL;
    void *workspace2_allocation = NULL;
    void *workspace1_second = NULL;
    enum kernel_heap_owner workspace1 = KERNEL_HEAP_OWNER_FIRST_WORKSPACE;
    enum kernel_heap_owner workspace2 =
        (enum kernel_heap_owner)(KERNEL_HEAP_OWNER_FIRST_WORKSPACE + 1);
    if (kernel_heap_allocate(&boot_heap, KERNEL_HEAP_OWNER_KERNEL, 17,
            &kernel_allocation) != KERNEL_HEAP_OK ||
        kernel_heap_allocate(&boot_heap, KERNEL_HEAP_OWNER_SESSION, 32,
            &session_allocation) != KERNEL_HEAP_OK ||
        kernel_heap_allocate(&boot_heap, workspace1, 16,
            &workspace1_first) != KERNEL_HEAP_OK ||
        kernel_heap_allocate(&boot_heap, workspace2, 16,
            &workspace2_allocation) != KERNEL_HEAP_OK ||
        kernel_heap_allocate(&boot_heap, workspace1, 16,
            &workspace1_second) != KERNEL_HEAP_OK ||
        (uintptr_t)kernel_allocation % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        (uintptr_t)session_allocation % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        (uintptr_t)workspace1_first % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        (uintptr_t)workspace2_allocation % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        (uintptr_t)workspace1_second % KERNEL_HEAP_ALIGNMENT_BYTES != 0 ||
        !heap_bytes_write_and_verify(kernel_allocation, 32, UINT8_C(0xa1)) ||
        !heap_bytes_write_and_verify(session_allocation, 32, UINT8_C(0xb2)) ||
        !heap_bytes_write_and_verify(workspace1_first, 16, UINT8_C(0xc3)) ||
        !heap_bytes_write_and_verify(workspace2_allocation, 16, UINT8_C(0xd4)) ||
        !heap_bytes_write_and_verify(workspace1_second, 16, UINT8_C(0xe5)) ||
        boot_heap.owner_bytes[KERNEL_HEAP_OWNER_KERNEL] != 32 ||
        boot_heap.owner_bytes[KERNEL_HEAP_OWNER_SESSION] != 32 ||
        boot_heap.owner_bytes[workspace1] != 32 ||
        boot_heap.owner_allocations[workspace1] != 2 ||
        boot_heap.owner_bytes[workspace2] != 16 ||
        !kernel_heap_check(&boot_heap)) {
        return heap_probe_failure(write_text, physical_address, true);
    }

    /* A wrong owner and an over-capacity request must preserve live ownership,
     * output and dirty payload. The subsequent valid free/allocation proves the
     * OOM was recoverable rather than retiring or partially mutating the heap. */
    void *oom_output = kernel_allocation;
    if (kernel_heap_release(&boot_heap, KERNEL_HEAP_OWNER_KERNEL,
            session_allocation) != KERNEL_HEAP_WRONG_OWNER ||
        kernel_heap_allocate(&boot_heap, KERNEL_HEAP_OWNER_KERNEL,
            MEMORY_PAGE_BYTES, &oom_output) != KERNEL_HEAP_OOM ||
        oom_output != kernel_allocation ||
        !heap_bytes_have_value(kernel_allocation, 32, UINT8_C(0xa1)) ||
        !heap_bytes_have_value(session_allocation, 32, UINT8_C(0xb2)) ||
        !heap_bytes_have_value(workspace1_first, 16, UINT8_C(0xc3)) ||
        !heap_bytes_have_value(workspace2_allocation, 16, UINT8_C(0xd4)) ||
        !heap_bytes_have_value(workspace1_second, 16, UINT8_C(0xe5)) ||
        !kernel_heap_check(&boot_heap)) {
        return heap_probe_failure(write_text, physical_address, true);
    }

    /* M5 workspace reset is owner-scoped. The arena backing frame is still
     * kernel-owned, so neither M2 pool counter may change. Session teardown is
     * deliberately a separate call and must retain kernel/workspace 2 bytes. */
    if (kernel_heap_release_workspace(&boot_heap, workspace1) != KERNEL_HEAP_OK ||
        boot_heap.owner_bytes[workspace1] != 0 ||
        boot_heap.owner_allocations[workspace1] != 0 ||
        boot_heap.owner_bytes[KERNEL_HEAP_OWNER_KERNEL] != 32 ||
        boot_heap.owner_bytes[KERNEL_HEAP_OWNER_SESSION] != 32 ||
        boot_heap.owner_bytes[workspace2] != 16 ||
        boot_allocator.ordinary_free != ordinary_before - 1 ||
        boot_allocator.reserve_free != reserve_before ||
        !heap_bytes_have_value(kernel_allocation, 32, UINT8_C(0xa1)) ||
        !heap_bytes_have_value(session_allocation, 32, UINT8_C(0xb2)) ||
        !heap_bytes_have_value(workspace2_allocation, 16, UINT8_C(0xd4)) ||
        !kernel_heap_check(&boot_heap) ||
        kernel_heap_release_session(&boot_heap) != KERNEL_HEAP_OK ||
        boot_heap.owner_bytes[KERNEL_HEAP_OWNER_SESSION] != 0 ||
        boot_heap.owner_allocations[KERNEL_HEAP_OWNER_SESSION] != 0 ||
        boot_heap.owner_bytes[KERNEL_HEAP_OWNER_KERNEL] != 32 ||
        boot_heap.owner_bytes[workspace2] != 16 ||
        boot_allocator.ordinary_free != ordinary_before - 1 ||
        boot_allocator.reserve_free != reserve_before ||
        !heap_bytes_have_value(kernel_allocation, 32, UINT8_C(0xa1)) ||
        !heap_bytes_have_value(workspace2_allocation, 16, UINT8_C(0xd4)) ||
        !kernel_heap_check(&boot_heap)) {
        return heap_probe_failure(write_text, physical_address, true);
    }

    void *reused = NULL;
    if (kernel_heap_release(&boot_heap, KERNEL_HEAP_OWNER_KERNEL,
            kernel_allocation) != KERNEL_HEAP_OK ||
        kernel_heap_allocate(&boot_heap, KERNEL_HEAP_OWNER_KERNEL, 17,
            &reused) != KERNEL_HEAP_OK ||
        reused != kernel_allocation ||
        !heap_bytes_have_value(reused, 32, 0) ||
        !heap_bytes_write_and_verify(reused, 32, UINT8_C(0xf6)) ||
        kernel_heap_release(&boot_heap, KERNEL_HEAP_OWNER_KERNEL,
            reused) != KERNEL_HEAP_OK ||
        kernel_heap_release(&boot_heap, workspace2,
            workspace2_allocation) != KERNEL_HEAP_OK ||
        !kernel_heap_check(&boot_heap) || boot_heap.live_allocations != 0 ||
        boot_heap.allocated_bytes != 0 ||
        boot_heap.free_bytes != MEMORY_PAGE_BYTES ||
        boot_heap.active_blocks != 1) {
        return heap_probe_failure(write_text, physical_address, true);
    }
    for (size_t owner = KERNEL_HEAP_OWNER_KERNEL;
         owner < KERNEL_HEAP_OWNER_COUNT; ++owner) {
        if (boot_heap.owner_bytes[owner] != 0 ||
            boot_heap.owner_allocations[owner] != 0) {
            return heap_probe_failure(write_text, physical_address, true);
        }
    }
    heap_probe_retire_descriptor();
    if (frame_release(&boot_allocator, FRAME_OWNER_KERNEL,
            physical_address) != FRAME_OK ||
        !frame_allocator_check(&boot_allocator) ||
        boot_allocator.ordinary_free != ordinary_before ||
        boot_allocator.reserve_free != reserve_before) {
        return heap_probe_failure(write_text, 0, false);
    }
    return write_text("SPIFFOS_OWNER_CLEANUP_OK\r\n") &&
        write_text("SPIFFOS_HEAP_OK\r\n");
}

/* Retained service storage is kernel metadata, never inside its owned arena.
 * Exactly one ordinary physical frame remains live for this boot lifetime. */
static struct memory_service retained_service;
static struct page_access_policy retained_policy;
static uint64_t retained_physical;
static uint64_t retained_free_before;
static bool retained_started;
/* These few additional ordinary KERNEL-owned frames hold private APIC page
 * tables. Retained-service expectations account for them explicitly; never
 * release the existing 4KiB heap frame or use the supervisor reserve. */
enum { MMIO_TABLE_LIMIT=4 };
static uint64_t mmio_table_owned[MMIO_TABLE_LIMIT];
static size_t mmio_table_count;
static bool mmio_table_poisoned;


/* End every service borrow before returning backing on a fatal probe failure.
 * No success marker follows a failure; caller stops boot. Exclusive boot CPU. */
static bool retained_failure(bool (*writer)(const char *), bool owns_frame)
{
    retained_started = false;
    memory_service_retire(&retained_service);
    if (owns_frame) {
        (void)frame_release(&boot_allocator, FRAME_OWNER_KERNEL, retained_physical);
    }
    (void)writer("SPIFFOS_MEMORY_SERVICE_ERROR\r\n");
    return false;
}

/* Revalidate the backing authority, roots and expected lifetimes before reads.
 * Called again after screen initialization; never allocates or changes state. */
static bool retained_valid(void)
{
    uint64_t mapped;
    const struct memory_service_session *session = NULL;
    if (!retained_started || !frame_allocator_check(&boot_allocator) ||
        boot_allocator.ordinary_free != retained_free_before - 1 - mmio_table_count ||
        boot_allocator.reserve_free != BOOT_RESERVE_FRAMES ||
        boot_allocator.owned[FRAME_OWNER_KERNEL] != 1 + mmio_table_count ||
        page_access_resolve(&retained_policy, FRAME_OWNER_KERNEL,
            retained_physical, &mapped) != PAGE_ACCESS_OK ||
        mapped != (uintptr_t)retained_service.heap.arena ||
        memory_service_check(&retained_service) != MEMORY_SERVICE_OK ||
        retained_service.heap.live_allocations != 10 ||
        memory_service_get_session(&retained_service, &session) != MEMORY_SERVICE_OK ||
        session->workspace_count != 9 || session->active_workspace_id != 1) {
        return false;
    }
    for (size_t id = 1; id <= MEMORY_SERVICE_WORKSPACE_COUNT; ++id) {
        const struct memory_service_workspace *workspace = NULL;
        if (memory_service_get_workspace(&retained_service, id, &workspace) != MEMORY_SERVICE_OK ||
            workspace->workspace_id != id ||
            workspace->reset_generation != (id == 1 ? 2u : 1u)) {
            return false;
        }
    }
    return true;
}

/* Acquire checked ordinary backing and retain ten roots after reversible probes.
 * Only temporary client blocks are released. Missing mapping/OOM preserves the
 * static editor path; corruption, self-test or serial errors cannot claim OK.
 * Borrow responses/writer synchronously; one CPU, interrupts disabled. */
bool memory_boot_service_start(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging,
    bool (*writer)(const char *))
{
    if (writer == NULL) return false;
    if (retained_started || retained_service.retired) return retained_failure(writer, retained_started);
    if (!boot_allocator_available ||
        !memory_boot_page_policy(hhdm, paging, &boot_allocator, &retained_policy)) {
        return writer("SPIFFOS_MEMORY_SERVICE_UNAVAILABLE\r\n");
    }
    if (!frame_allocator_check(&boot_allocator)) return retained_failure(writer, false);
    retained_free_before = boot_allocator.ordinary_free;
    enum page_access_result acquired = page_access_allocate_zero(&boot_allocator,
        &retained_policy, FRAME_OWNER_KERNEL, kernel_page_zero, NULL, &retained_physical);
    if (acquired != PAGE_ACCESS_OK) {
        if (acquired == PAGE_ACCESS_ALLOCATION_FAILED && retained_free_before == 0) {
            return writer("SPIFFOS_MEMORY_SERVICE_UNAVAILABLE\r\n");
        }
        return retained_failure(writer, false);
    }
    uint64_t mapped;
    if (page_access_resolve(&retained_policy, FRAME_OWNER_KERNEL, retained_physical,
            &mapped) != PAGE_ACCESS_OK ||
        memory_service_initialize(&retained_service, (void *)(uintptr_t)mapped,
            MEMORY_PAGE_BYTES) != MEMORY_SERVICE_OK) return retained_failure(writer, true);
    void *first = NULL, *second = NULL;
    const struct memory_service_session *session = NULL;
    const struct memory_service_workspace *other = NULL;
    enum kernel_heap_owner one = KERNEL_HEAP_OWNER_FIRST_WORKSPACE;
    enum kernel_heap_owner two = KERNEL_HEAP_OWNER_FIRST_WORKSPACE + 1;
    if (memory_service_get_session(&retained_service, &session) != MEMORY_SERVICE_OK ||
        memory_service_get_workspace(&retained_service, 2, &other) != MEMORY_SERVICE_OK ||
        memory_service_allocate(&retained_service, one, 32, &first) != MEMORY_SERVICE_OK ||
        memory_service_allocate(&retained_service, two, 32, &second) != MEMORY_SERVICE_OK ||
        !heap_bytes_have_value(first, 32, 0) || !heap_bytes_have_value(second, 32, 0) ||
        !heap_bytes_write_and_verify(first, 32, 0xa5) ||
        !heap_bytes_write_and_verify(second, 32, 0x5a) ||
        memory_service_release(&retained_service, one, second) != MEMORY_SERVICE_WRONG_OWNER ||
        memory_service_release(&retained_service, KERNEL_HEAP_OWNER_SESSION,
            (void *)session) != MEMORY_SERVICE_ROOT_PROTECTED ||
        memory_service_reset_workspace(&retained_service, 1) != MEMORY_SERVICE_OK ||
        !heap_bytes_have_value(second, 32, 0x5a) ||
        retained_service.session_root != session || retained_service.workspace_roots[1] != other ||
        memory_service_release(&retained_service, two, second) != MEMORY_SERVICE_OK) {
        return retained_failure(writer, true);
    }
    retained_started = true;
    if (!retained_valid()) return retained_failure(writer, true);
    return writer("MEMORY_SERVICE:4096,1,1,9,") &&
        write_number(writer, boot_allocator.ordinary_free) && writer(",") &&
        write_number(writer, boot_allocator.reserve_free) &&
        writer("\r\nSPIFFOS_MEMORY_SERVICE_OK\r\n");
}

/* Verify retained state after framebuffer/console setup. Unsupported startup
 * already emitted UNAVAILABLE; a serial or invariant failure stops boot. */
bool memory_boot_service_check(bool (*writer)(const char *))
{
    if (writer == NULL) return false;
    if (!retained_started) return true;
    if (!retained_valid()) return retained_failure(writer, true);
    return writer("SPIFFOS_MEMORY_RETAINED_OK\r\n");
}

/* Repeat the retained-state proof after screen_editor_initialize. This second
 * phase has its own marker so tests prove ordering without duplicate success. */
bool memory_boot_service_editor_check(bool (*writer)(const char *))
{
    if (writer == NULL) return false;
    if (!retained_started) return true;
    if (!retained_valid()) return retained_failure(writer, true);
    return writer("SPIFFOS_MEMORY_EDITOR_OK\r\n");
}

/* Mutate only the retained session root, preserving M6 ownership and all
 * other workspace roots. The display manager prepares its target first. */
bool memory_boot_select_workspace(size_t workspace_id)
{
    return retained_started &&
        memory_service_select_workspace(&retained_service, workspace_id) == MEMORY_SERVICE_OK;
}

/* Delegate isolated owner cleanup to the validated memory service. */
bool memory_boot_reset_workspace(size_t workspace_id)
{
    return retained_started &&
        memory_service_reset_workspace(&retained_service, workspace_id) == MEMORY_SERVICE_OK;
}


/* Acquire one permanent KERNEL-owned page-table frame through M2/M3.
 * Zeroing and physical resolution occur before publication. A failed M3
 * rollback poisons the service so no still-owned RAM is accidentally reused. */
bool memory_boot_mmio_table_acquire(uint64_t *physical, uint64_t **mapped)
{
    if(physical==NULL || mapped==NULL || mmio_table_poisoned ||
       !retained_started || !boot_allocator_available ||
       mmio_table_count>=MMIO_TABLE_LIMIT ||
       retained_policy.allocator!=&boot_allocator ||
       !frame_allocator_check(&boot_allocator)) return false;
    uint64_t acquired_phys=0;
    enum page_access_result acquired=page_access_allocate_zero(
        &boot_allocator,&retained_policy,FRAME_OWNER_KERNEL,
        kernel_page_zero,NULL,&acquired_phys);
    if(acquired!=PAGE_ACCESS_OK) {
        if(acquired==PAGE_ACCESS_ROLLBACK_FAILED) mmio_table_poisoned=true;
        return false;
    }
    uint64_t mapped_address=0;
    if(page_access_resolve(&retained_policy,FRAME_OWNER_KERNEL,
            acquired_phys,&mapped_address)!=PAGE_ACCESS_OK) {
        if(frame_release(&boot_allocator,FRAME_OWNER_KERNEL,
                         acquired_phys)!=FRAME_OK) mmio_table_poisoned=true;
        return false;
    }
    mmio_table_owned[mmio_table_count]=acquired_phys;
    ++mmio_table_count;
    *physical=acquired_phys;
    *mapped=(uint64_t *)(uintptr_t)mapped_address;
    return true;
}

/* Release only a precise table page acquired above, after its parent link
 * has been removed. Wrong/stale addresses are rejected without free. */
bool memory_boot_mmio_table_release(uint64_t physical)
{
    if(mmio_table_poisoned || physical==0 || !retained_started ||
       !frame_allocator_check(&boot_allocator)) return false;
    for(size_t i=0;i<mmio_table_count;++i) {
        if(mmio_table_owned[i]!=physical) continue;
        if(frame_release(&boot_allocator,FRAME_OWNER_KERNEL,physical)!=FRAME_OK) {
            mmio_table_poisoned=true;
            return false;
        }
        --mmio_table_count;
        mmio_table_owned[i]=mmio_table_owned[mmio_table_count];
        mmio_table_owned[mmio_table_count]=0;
        return true;
    }
    return false;
}

/* Snapshot pages are ordinary M2-owned frames, not bootloader ELF storage.
 * Each is validated by M3 before a pointer can be published. Storage is
 * private to one workspace, persistent until shutdown, and unavailable to IRQs.
 * The reserved 64-page supervisor pool is never consumed. */
enum { SNAPSHOT_WORKSPACES = 9, SNAPSHOT_PAGES = 1000 };
static uint64_t snapshot_phys[SNAPSHOT_WORKSPACES][SNAPSHOT_PAGES];
static uint32_t *snapshot_virt[SNAPSHOT_WORKSPACES][SNAPSHOT_PAGES];
static size_t snapshot_page_counts[SNAPSHOT_WORKSPACES];
/* Any rollback failure invalidates the allocator for further snapshots;
 * attempting another allocation could reuse live untracked physical pages. */
static bool snapshot_rollback_failed;

/* All-or-nothing page reservation for workspace_id (1..9). out_pages belongs
 * to the caller and is untouched on failure. Incomplete allocations are
 * released with their exact owner. Boot CPU only; no interrupts, locks or I/O. */
bool memory_boot_snapshot_prepare(size_t workspace_id, size_t pages,
                                  uint32_t **out_pages)
{
    if (snapshot_rollback_failed || !retained_started ||
        !boot_allocator_available || workspace_id < 1 || workspace_id > SNAPSHOT_WORKSPACES ||
        pages == 0 || pages > SNAPSHOT_PAGES || out_pages == NULL ||
        retained_policy.allocator != &boot_allocator) return false;
    size_t slot = workspace_id - 1;
    enum frame_owner owner = (enum frame_owner)(FRAME_OWNER_FIRST_WORKSPACE + slot);
    if (snapshot_page_counts[slot] != 0) {
        if (snapshot_page_counts[slot] != pages) return false;
        for (size_t i = 0; i < pages; ++i)
            if (snapshot_virt[slot][i] == NULL) return false;
        for (size_t i = 0; i < pages; ++i)
            out_pages[i] = snapshot_virt[slot][i];
        return true;
    }
    size_t completed = 0;
    for (; completed < pages; ++completed) {
        uint64_t physical = UINT64_MAX;
        enum page_access_result acquired =
            page_access_allocate_zero(&boot_allocator, &retained_policy, owner,
                                      kernel_page_zero, NULL, &physical);
        if (acquired != PAGE_ACCESS_OK) {
            if (acquired == PAGE_ACCESS_ROLLBACK_FAILED)
                snapshot_rollback_failed = true;
            break;
        }
        uint64_t mapped = 0;
        if (page_access_resolve(&retained_policy, owner, physical, &mapped) !=
              PAGE_ACCESS_OK) {
            if (frame_release(&boot_allocator, owner, physical) != FRAME_OK)
                snapshot_rollback_failed = true;
            break;
        }
        snapshot_phys[slot][completed] = physical;
        snapshot_virt[slot][completed] = (uint32_t *)(uintptr_t)mapped;
    }
    if (completed < pages) {
        while (completed > 0) {
            --completed;
            if (frame_release(&boot_allocator, owner,
                              snapshot_phys[slot][completed]) != FRAME_OK)
                snapshot_rollback_failed = true;
            snapshot_phys[slot][completed] = 0;
            snapshot_virt[slot][completed] = NULL;
        }
        return false;
    }
    snapshot_page_counts[slot] = pages;
    for (size_t i = 0; i < pages; ++i)
        out_pages[i] = snapshot_virt[slot][i];
    return true;
}

#ifdef SPIFFOS_M10_B_SWITCH_TEST
/* M10-B first owned-stack QEMU proof. Each 16 KiB stack must be made of
 * exactly four contiguous, individually M2-owned ordinary KERNEL frames.
 * Contiguity is NOT assumed from M2's allocation order: it is verified
 * before a single pointer becomes visible to the caller. Allocations occur
 * only with IF=0, after retained-service validation and before its next
 * read-only check; the test returns these frames before keyboard startup. */
enum { M10_B_TEST_STACKS=2, M10_B_STACK_PAGES=4 };
static uint64_t m10_b_stack_physical[M10_B_TEST_STACKS][M10_B_STACK_PAGES];
static bool m10_b_stack_active[M10_B_TEST_STACKS];
static bool m10_b_stack_poisoned;
/* Only the isolated guest test may force a deterministic partial allocation. */
static size_t m10_b_forced_failure_index=SIZE_MAX;

/* Release a partially acquired stack in reverse order. An unexpected owner
 * mismatch is fatal for this test allocator; never pretend backing is free. */
static bool m10_b_stack_rollback(size_t index, size_t acquired)
{
    bool clean=true;
    while(acquired>0) {
        --acquired;
        if(frame_release(&boot_allocator,FRAME_OWNER_KERNEL,
                m10_b_stack_physical[index][acquired])!=FRAME_OK) clean=false;
        m10_b_stack_physical[index][acquired]=0;
    }
    if(!clean) m10_b_stack_poisoned=true;
    return clean;
}

/* Acquire mapped, zeroed ordinary pages without using the supervisor pool.
 * Any failed mapping/contiguity check rolls back every acquired frame. */
bool memory_boot_m10_task_stack_prepare(size_t index, void **out_stack)
{
    if(index>=M10_B_TEST_STACKS || out_stack==NULL || m10_b_stack_poisoned ||
       m10_b_stack_active[index] || !retained_started ||
       !boot_allocator_available ||
       retained_policy.allocator!=&boot_allocator ||
       !frame_allocator_check(&boot_allocator)) return false;
    uint64_t first_physical=0, first_virtual=0;
    size_t acquired=0;
    for(;acquired<M10_B_STACK_PAGES;++acquired) {
        /* Inject a refusal BEFORE the next frame is claimed, so rollback
         * must release precisely the earlier frames and preserve outputs. */
        if(acquired==m10_b_forced_failure_index) break;
        uint64_t physical=UINT64_MAX,virtual_address=0;
        enum page_access_result status=page_access_allocate_zero(
            &boot_allocator,&retained_policy,FRAME_OWNER_KERNEL,
            kernel_page_zero,NULL,&physical);
        if(status!=PAGE_ACCESS_OK) {
            if(status==PAGE_ACCESS_ROLLBACK_FAILED) m10_b_stack_poisoned=true;
            break;
        }
        m10_b_stack_physical[index][acquired]=physical;
        /* Count this frame as owned even when address resolution fails. */
        if(page_access_resolve(&retained_policy,FRAME_OWNER_KERNEL,physical,
              &virtual_address)!=PAGE_ACCESS_OK) {
            ++acquired;
            break;
        }
        if(acquired==0) {
            first_physical=physical;
            first_virtual=virtual_address;
        } else if(first_physical > UINT64_MAX - 4096u*acquired ||
                  first_virtual > UINT64_MAX - 4096u*acquired ||
                  physical!=first_physical+4096u*acquired ||
                  virtual_address!=first_virtual+4096u*acquired) {
            ++acquired;
            break;
        }
    }
    if(acquired!=M10_B_STACK_PAGES) {
        (void)m10_b_stack_rollback(index,acquired);
        return false;
    }
    m10_b_stack_active[index]=true;
    *out_stack=(void *)(uintptr_t)first_virtual;
    return true;
}

/* Retire backing only after the caller has returned to a different stack.
 * The exact physical slots are released; a second call must fail. */
bool memory_boot_m10_task_stack_release(size_t index)
{
    if(index>=M10_B_TEST_STACKS || m10_b_stack_poisoned ||
       !m10_b_stack_active[index] ||
       !frame_allocator_check(&boot_allocator)) return false;
    if(!m10_b_stack_rollback(index,M10_B_STACK_PAGES)) return false;
    m10_b_stack_active[index]=false;
    return frame_allocator_check(&boot_allocator);
}

/* Diagnostic fail-after-two-pages proof. Require complete rollback of frame
 * ownership, ordinary/reserve counts and caller output before continuing.
 * Called only with IF=0 from the temporary M10-B guest smoke test. */
bool memory_boot_m10_task_stack_failure_probe(void)
{
    if(!retained_started || m10_b_stack_active[0] ||
       m10_b_stack_active[1] || m10_b_stack_poisoned ||
       !frame_allocator_check(&boot_allocator)) return false;
    uint64_t ordinary_before=boot_allocator.ordinary_free;
    uint64_t reserved_before=boot_allocator.reserve_free;
    uint64_t kernel_before=boot_allocator.owned[FRAME_OWNER_KERNEL];
    void *sentinel=(void *)(uintptr_t)UINT64_C(0x12340);
    m10_b_forced_failure_index=2;
    bool unexpectedly_allocated=
        memory_boot_m10_task_stack_prepare(0,&sentinel);
    m10_b_forced_failure_index=SIZE_MAX;
    return !unexpectedly_allocated &&
           sentinel==(void *)(uintptr_t)UINT64_C(0x12340) &&
           !m10_b_stack_active[0] && !m10_b_stack_poisoned &&
           boot_allocator.ordinary_free==ordinary_before &&
           boot_allocator.reserve_free==reserved_before &&
           boot_allocator.owned[FRAME_OWNER_KERNEL]==kernel_before &&
           frame_allocator_check(&boot_allocator);
}
#endif
