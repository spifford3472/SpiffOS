/* Purpose: fail-closed cache/MTRR/HHDM-alias gate for APIC device reads.
 * Dependencies: x86-page-walk.h and existing validated PAT PTE policy.
 * Invariants: pure snapshot validation; never touches MSRs or MMIO here.
 * Reference: docs/decisions/ADR-m9-3d5-apic-readonly.md. */
#ifndef SPIFFOS_APIC_MEMORY_TYPE_H
#define SPIFFOS_APIC_MEMORY_TYPE_H
#include <stdbool.h>
#include <stdint.h>
#include "x86-page-walk.h"

/* Caller gathers CPU state with CPUID/RDMSR only after feature checks.
 * HHDM aliases must be **proven absent**, not merely uninspectable.
 * The private 4KiB leaf's PAT=0/PCD=PWT=1 selects PAT index 3 UC. */
struct apic_memory_type_evidence {
    bool mtrr_cpuid_supported;
    uint64_t ia32_mtrrcap;
    uint64_t ia32_mtrr_def_type;
    uint64_t ia32_pat;
    enum x86_page_probe_status lapic_hhdm;
    enum x86_page_probe_status ioapic_hhdm;
};

/* Accept only an enabled conventional MTRR configuration with at most 16
 * variable ranges, UC at PAT index3 and demonstrably absent HHDM aliases.
 * The UC PAT entry forces an effective UC access for this aperture; no
 * inference about uninspected aliases or unsupported memory layouts.
 * No mutation; success/failure has no side effects. */
bool apic_memory_type_safe(const struct apic_memory_type_evidence *evidence);
#endif
