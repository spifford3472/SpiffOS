/* Purpose: report validated candidate RAM without changing mappings or frames.
 * Dependencies: pinned limine.h, pure memory-map inventory and serial callback.
 * Invariants: static bounded metadata, one startup CPU, interrupts disabled.
 * References: ADR-memory-foundation.md and ADR-011; not a frame allocator. */
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "memory-map.h"
#include "memory-boot.h"

_Static_assert(MEMORY_REGION_USABLE == LIMINE_MEMMAP_USABLE,
    "inventory usable type must match the pinned Limine header");

/* Only metadata is copied here. A 12KiB snapshot belongs in BSS rather than
 * Limine's entry stack; borrowed response pointers are never modified. */
static struct memory_region snapshot[MEMORY_MAP_MAX_ENTRIES];

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
    return write_text("MEMORY_SUMMARY:") &&
        write_number(write_text, inventory.usable_bytes) && write_text(",") &&
        write_number(write_text, inventory.candidate_frames) && write_text(",") &&
        write_number(write_text, inventory.candidate_bytes) && write_text(",") &&
        write_number(write_text, inventory.excluded_usable_bytes) &&
        write_text("\r\nSPIFFOS_MEMORY_OK\r\n");
}
