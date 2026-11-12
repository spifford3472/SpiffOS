/* Purpose: validate x86 local APIC capability against ACPI topology.
 * Dependencies: acpi-madt.h; caller-provided CPU register snapshot.
 * Invariants: no MMIO, IRQ unmask, arbitrary MSR read, heap or pointer mapping.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#ifndef APIC_PREFLIGHT_H
#define APIC_PREFLIGHT_H
#include <stdbool.h>
#include <stdint.h>
#include "acpi-madt.h"

/* CPUID leaf 1 EDX and IA32_APIC_BASE MSR are read at boot by the caller.
 * The CPU register snapshot is not a hardware route or MMIO mapping permit. */
struct apic_cpu_snapshot {
    uint32_t cpuid_leaf1_edx;
    uint64_t ia32_apic_base_msr;
    uint8_t cpuid_apic_id;
};

/* Verify xAPIC support/enabled state and unambiguous MADT physical topology.
 * Output physical addresses are already present in caller-owned topology;
 * no MMIO mapping or device operations occur here. */
bool apic_cpu_topology_preflight(const struct acpi_apic_topology *topology,
                                 const struct apic_cpu_snapshot *cpu);

/* Obtain only CPUID 1 and standard IA32_APIC_BASE MSR after confirming APIC
 * capability bit; never reads APIC MMIO or enables device interrupts.
 * Boot CPU only, CPL0, IF=0. false means unsupported hardware state. */
bool apic_cpu_read_snapshot(struct apic_cpu_snapshot *out);
#endif
