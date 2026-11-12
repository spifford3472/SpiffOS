/* Purpose: bounded boot-CPU xAPIC + IOAPIC PIT0 IRQ route.
 * Dependencies: validated M9-3D5 MADT/CPUID/mapping/cache evidence.
 * Invariants: only IRQ0's IOAPIC redirection is mutated, all other pins
 * verified masked; no IRQ enable, heap, logging or scheduling in this API.
 * Reference: docs/decisions/ADR-m9-3d6-irq0-delivery.md. */
#ifndef SPIFFOS_APIC_IRQ0_H
#define SPIFFOS_APIC_IRQ0_H
#include <stdbool.h>
#include <stdint.h>
#include "acpi-madt.h"

enum { APIC_IRQ0_VECTOR=0x30, APIC_SPURIOUS_VECTOR=0xff };

/* A backend must accept only aligned LAPIC 4-byte offsets or IOAPIC
 * IOREGSEL(0)/IOWIN(0x10). Errors cannot be silently retried. */
struct apic_irq0_io {
    bool (*read)(void *context, bool local, uint32_t offset, uint32_t *value);
    bool (*write)(void *context, bool local, uint32_t offset, uint32_t value);
    void *context;
};
struct apic_irq0_route {
    uint8_t irq0_pin;
    uint8_t destination_apic_id;
    uint32_t saved_redirection_low;
    uint32_t saved_redirection_high;
    uint32_t saved_svr;
    bool prepared;
    bool unmasked;
    bool poisoned; /* rollback/unmask readback failure: no STI */
};

/* Read-only whole-controller preflight. Require one MADT-confirmed LAPIC,
 * valid low/edge ISA flags, 2..240 pins and ALL IOAPIC pins masked.
 * Reject unmasked LINT0 ExtINT or nonzero TPR; no controller configuration
 * writes on invalid (IOAPIC read selection still touches IOREGSEL latch).
 * Empty state out is initialized only after all read-only checks succeed.
 * IF must already be 0, no other APIC clients; never an IRQ-context API. */
bool apic_irq0_probe(const struct apic_irq0_io *io,
                     const struct acpi_apic_topology *topology,
                     uint8_t cpu_apic_id, uint16_t redirection_entries,
                     struct apic_irq0_route *out);

/* Stage masked IRQ0 low/high redirection, then enable LAPIC software
 * spurious-vector routing. Caller must install 0x30/0xff IDT gates first.
 * IF=0 throughout, output poisoned on failed mutation/readback. */
bool apic_irq0_prepare(const struct apic_irq0_io *io,
                       struct apic_irq0_route *route);

/* Clear only IRQ0's mask bit after PIT0 is configured; verify exact
 * read-back before STI. On failure caller must remain IF=0. */
bool apic_irq0_unmask(const struct apic_irq0_io *io,
                      struct apic_irq0_route *route);

/* Send EOI to local APIC register 0xb0. Use only from short IRQ0 handler
 * after successful unmask and live IF enable. A failed hardware backend
 * should cause fatal platform shutdown rather than drop acknowledgment. */
bool apic_irq0_eoi(const struct apic_irq0_io *io);
#endif
