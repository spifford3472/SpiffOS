/* Purpose: parse a complete, already mapped ACPI MADT into bounded topology.
 * Dependencies: C11 integer types; no hardware or memory allocator.
 * Invariants: caller owns table bytes; no raw physical pointer dereference;
 * rejects ambiguity/invalid input without changing the caller's output.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#ifndef ACPI_MADT_H
#define ACPI_MADT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct acpi_apic_topology {
    uint64_t lapic_physical;
    uint32_t ioapic_physical;
    uint32_t ioapic_gsi_base;
    uint32_t irq0_gsi;
    uint16_t irq0_flags;
    uint8_t ioapic_id;
    uint8_t enabled_lapics;
    uint8_t boot_lapic_id; /* only one enabled xAPIC supported in D5 */
};

/* Check a firmware byte span (including checksum itself). Reject empty,
 * absent, oversized and malformed inputs without accessing outside length. */
bool acpi_bytes_checksum_valid(const uint8_t *bytes, size_t length);

/* Pure parser for a complete, mapped MADT with ACPI SDT checksum.
 * Accepts one IOAPIC, at least one enabled LAPIC, optional LAPIC address
 * override and a single ISA IRQ0 interrupt source override. Rejects multiple
 * IOAPICs until GSI-range discovery is supported, reserved ISO flags,
 * malformed entries, absent LAPIC MMIO and arithmetic overflows.
 * On failure out remains untouched; never dereferences MMIO addresses. */
bool acpi_madt_parse(const uint8_t *table, size_t available,
                     struct acpi_apic_topology *out);
#endif
