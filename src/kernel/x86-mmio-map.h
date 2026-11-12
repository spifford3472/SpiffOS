/* Purpose: atomic publication and rollback of two x86 APIC MMIO mappings.
 * Dependencies: mmio-page-policy.h and caller-verified owned RAM table pages.
 * Invariants: only a previously empty, kernel-private root slot is mutated;
 * stage lower-level table pages first, commit root last; IF=0, boot CPU only.
 * Reference: docs/decisions/ADR-m9-3d4-mmio-mapping.md. */
#ifndef SPIFFOS_X86_MMIO_MAP_H
#define SPIFFOS_X86_MMIO_MAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mmio-page-policy.h"

#define MMIO_APIC_LAPIC_VIRTUAL UINT64_C(0xffffc80000000000)
#define MMIO_APIC_IOAPIC_VIRTUAL UINT64_C(0xffffc80000001000)
enum { MMIO_TABLE_MAX_PAGES=4 };

/* Backend owns mappings and exact-owner page allocation/release; it must
 * supply zeroed 4KiB writable RAM pages and keep them valid until released.
 * The callback never allocates from the supervisor reserve. Invalidation is
 * a processor-local INVLPG for one virtual address after a PTE change. */
struct x86_mmio_map_backend {
    bool (*acquire)(void *context, uint64_t *physical, uint64_t **writable);
    bool (*release)(void *context, uint64_t physical);
    void (*invalidate)(void *context, uint64_t virtual_address);
    void *context;
};

/* Record live page tables and the specific parent entry they are linked
 * through. Owned table pages cannot be released while active. */
struct x86_mmio_mapping {
    uint64_t physical[MMIO_TABLE_MAX_PAGES];
    uint64_t *writable[MMIO_TABLE_MAX_PAGES];
    uint64_t *root_entry;
    size_t table_count;
    bool active;
    bool poisoned; /* failed rollback; never retry */
};

/* Create exactly two 4KiB mappings at the fixed, unused supervisor slots.
 * Borrow a validated 4096-byte writable CR3 root mapping (not an APIC page).
 * Root must be a CPU-owned page-table frame; caller checks mapping/lifetime.
 * Inputs and state are boot-CPU-exclusive, interrupts disabled. On failure
 * root and all published links stay unchanged unless rollback itself fails,
 * in which case state.poisoned is set and caller must halt without STI. */
bool x86_mmio_map_create(struct x86_mmio_mapping *state,
    uint8_t levels, uint64_t *root, uint64_t lapic_physical,
    uint64_t ioapic_physical, const struct mmio_page_cpu_policy *policy,
    const struct x86_mmio_map_backend *backend);

/* Remove only this mapping's root link, invalidate both virtual addresses,
 * and return exact table frames to the backend. Any failed release poisons
 * state and demands a halt; caller must stop using virtual MMIO first. */
bool x86_mmio_map_destroy(struct x86_mmio_mapping *state,
    const struct x86_mmio_map_backend *backend);
#endif
