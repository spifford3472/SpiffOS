/* Purpose: adapt borrowed Limine metadata to serial-only M1 inventory.
 * Dependencies: caller-supplied synchronous NUL-string writer.
 * Invariants: boot CPU, interrupts disabled, no allocation/physical access.
 * Reference: docs/decisions/ADR-memory-foundation.md. */
#ifndef SPIFFOS_MEMORY_BOOT_H
#define SPIFFOS_MEMORY_BOOT_H
#include <stdbool.h>
struct limine_memmap_response;
/* Borrow all response mappings synchronously. Copy at most 512 entries into
 * private static state; never reclaim loader storage. Invalid/missing maps
 * emit UNAVAILABLE and return the writer result. Valid maps emit entries,
 * summary and MEMORY_OK in order. False means writer failure, possibly after
 * a prefix. No physical frame access, allocation, concurrency or ownership
 * transfer; one boot CPU caller only, interrupts disabled. */
bool memory_boot_report(const struct limine_memmap_response *response,
    bool (*write_text)(const char *text));
#endif
