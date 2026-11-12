/* Purpose: pure, checked x86-64 leaf PTE policy for private APIC MMIO.
 * Dependencies: C11 integer types, caller-verified IA32_PAT and NX state.
 * Invariants: never dereferences physical/virtual addresses or edits CR3;
 * output is unchanged on error and never maps existing nonempty slots.
 * References: docs/decisions/ADR-m9-3d4-mmio-mapping.md. */
#ifndef SPIFFOS_MMIO_PAGE_POLICY_H
#define SPIFFOS_MMIO_PAGE_POLICY_H
#include <stdbool.h>
#include <stdint.h>

enum mmio_page_result {
    MMIO_PAGE_OK=0,
    MMIO_PAGE_INVALID,
    MMIO_PAGE_CONFLICT,
    MMIO_PAGE_UNSUPPORTED
};

/* x86-64 register facts supplied by verified hardware preflight or tests.
 * PAT index 3 is selected by PTE PAT=0, PCD=1, PWT=1; it must read UC=0.
 * The kernel must also enable NXE in EFER before installing an NX leaf. */
struct mmio_page_cpu_policy {
    uint64_t ia32_pat_msr;
    bool nx_supported;
    bool nxe_enabled;
    uint8_t physical_address_bits; /* CPUID 0x80000008, 36..52 */
};

/* Compute a 4-KiB supervisor-only RW, NX, UC PTE for one empty leaf.
 * Inputs are page-aligned numeric addresses; virtual must be canonical
 * 48-bit high-half, nonzero, and physically supported by CPU width.
 * A NONZERO original entry is a conflict even if its Present bit is zero:
 * never overwrite software paging state. Success publishes once.
 * No hardware access, allocation, mutation or IRQ use. */
enum mmio_page_result mmio_page_plan_leaf(
    uint64_t virtual_address, uint64_t physical_address,
    uint64_t existing_entry, const struct mmio_page_cpu_policy *cpu,
    uint64_t *new_entry);
#endif
