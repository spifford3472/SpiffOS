/* Purpose: validate read-only LAPIC/IOAPIC identity and IRQ0 GSI capacity.
 * Dependencies: validated MADT, CPUID xAPIC ID, bounded injected MMIO reads.
 * Invariants: no IRQ controller configuration, PIT changes, EOI or STI.
 * Reference: docs/decisions/ADR-m9-3d5-apic-readonly.md. */
#ifndef SPIFFOS_APIC_READONLY_H
#define SPIFFOS_APIC_READONLY_H
#include <stdbool.h>
#include <stdint.h>
#include "acpi-madt.h"

/* Backend must accept only LAPIC ID register 0x20 and I/O APIC indices
 * 0x00 (ID) / 0x01 (version). I/O APIC indirect read necessarily requires
 * an IOREGSEL index-latch write, restored after reading IOWIN. No IRQ routing
 * configuration or other register write is permitted. */
struct apic_readonly_backend {
    bool (*lapic_id_read)(void *context, uint32_t *value);
    bool (*ioapic_index_read)(void *context, uint8_t index, uint32_t *value);
    void *context;
};
struct apic_readonly_result {
    uint8_t lapic_id;
    uint8_t ioapic_id;
    uint8_t ioapic_version;
    uint16_t redirection_entries;
    uint32_t irq0_gsi;
};

/* Verify boot CPU ID == MADT enabled LAPIC ID == LAPIC ID register; verify
 * IOAPICID matches MADT, IOAPICVER encodes 1..240 entries and IRQ0 GSI
 * falls inside actual I/O APIC pin range. Output unchanged on any error.
 * Caller first proves MMIO cache/alias safety and active 4KiB mappings. */
bool apic_readonly_validate(const struct acpi_apic_topology *topology,
                            uint8_t cpuid_lapic_id,
                            const struct apic_readonly_backend *backend,
                            struct apic_readonly_result *out);
#endif
