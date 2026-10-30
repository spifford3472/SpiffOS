/* Purpose: adapt validated Limine memory metadata to M1-M3 boot diagnostics.
 * Responsibilities: inventory candidate RAM, exercise M2 ownership, validate
 * the HHDM/paging mode, and touch only one currently owned M3 probe page.
 * Dependencies: pinned limine.h, M1/M2/M3 policy and a serial callback.
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

_Static_assert(MEMORY_REGION_USABLE == LIMINE_MEMMAP_USABLE,
    "inventory usable type must match the pinned Limine header");

/* Only metadata is copied here. A 12KiB snapshot belongs in BSS rather than
 * Limine's entry stack; borrowed response pointers are never modified. */
static struct memory_region snapshot[MEMORY_MAP_MAX_ENTRIES];
static size_t snapshot_count;
enum { BOOT_FRAME_CAPACITY = 4 * 1024 * 1024, BOOT_RESERVE_FRAMES = 64 };
/* Two bytes per candidate frame, up to 16GiB; Limine excludes this kernel BSS
 * from USABLE memory. The table does not live inside any candidate frame. */
static uint16_t frame_owners[BOOT_FRAME_CAPACITY];
static struct frame_allocator boot_allocator;
/* True only after the complete M2 probe restored its pools and emitted its
 * success marker. M3 must remain unavailable when M2 capacity was unsupported. */
static bool boot_allocator_available;

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
