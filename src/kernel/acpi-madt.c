/* Purpose: validate a firmware-owned ACPI MADT without any MMIO reads.
 * Dependencies: acpi-madt.h; ACPI 2+ MADT and SDT byte layouts.
 * Invariants: no heap or hardware access; bounded pointer arithmetic;
 * reject unverified/multiple IOAPIC topology; fail without output writes.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#include "acpi-madt.h"

enum {
    SDT_HEADER_BYTES=36, MADT_HEADER_BYTES=44, MAX_ACPI_TABLE_BYTES=65536,
    MADT_LOCAL_APIC=0, MADT_IOAPIC=1, MADT_INTERRUPT_OVERRIDE=2,
    MADT_LAPIC_OVERRIDE=5, ISA_BUS=0, ISA_IRQ_TIMER=0,
    ENTRY_LAPIC_BYTES=8, ENTRY_IOAPIC_BYTES=12,
    ENTRY_ISO_BYTES=10, ENTRY_LAPIC_OVERRIDE_BYTES=12
};

/* Explicit little-endian decoding avoids misaligned packed struct loads.
 * Caller must have independently proven the complete field is available. */
static uint16_t read_u16(const uint8_t *bytes)
{ return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8)); }

/* Decode a four-byte ACPI integer from a byte-validated span. */
static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Decode a little-endian physical address; never map it implicitly. */
static uint64_t read_u64(const uint8_t *bytes)
{ return read_u32(bytes) | ((uint64_t)read_u32(bytes + 4) << 32); }

/* ACPI checksums are byte sums modulo 256. Firmware data is borrowed;
 * a length cap prevents accidentally traversing a bogus huge table. */
bool acpi_bytes_checksum_valid(const uint8_t *bytes, size_t length)
{
    if (bytes == NULL || length == 0 || length > MAX_ACPI_TABLE_BYTES)
        return false;
    uint8_t sum = 0;
    for (size_t offset = 0; offset < length; ++offset)
        sum = (uint8_t)(sum + bytes[offset]);
    return sum == 0;
}

/* Parse only a small unambiguous subset; future multicore/multiple I/O APIC
 * support must have its own routing and validation ADR. The output is
 * transactional, so the caller never observes partially parsed topology. */
bool acpi_madt_parse(const uint8_t *table, size_t available,
                     struct acpi_apic_topology *out)
{
    if (table == NULL || out == NULL || available < MADT_HEADER_BYTES)
        return false;
    if (table[0] != 'A' || table[1] != 'P' ||
        table[2] != 'I' || table[3] != 'C') return false;
    uint32_t declared = read_u32(table + 4);
    if (declared < MADT_HEADER_BYTES || declared > available ||
        declared > MAX_ACPI_TABLE_BYTES ||
        !acpi_bytes_checksum_valid(table, declared)) return false;

    struct acpi_apic_topology parsed = {0};
    parsed.lapic_physical = read_u32(table + 36);
    parsed.irq0_gsi = 0;
    bool ioapic_seen = false, irq0_override_seen = false;
    bool lapic_override_seen = false;
    for (size_t offset = MADT_HEADER_BYTES; offset < declared;) {
        if (declared - offset < 2) return false;
        uint8_t type = table[offset];
        uint8_t entry_bytes = table[offset + 1];
        if (entry_bytes < 2 || entry_bytes > declared - offset)
            return false;
        const uint8_t *entry = table + offset;
        if (type == MADT_LOCAL_APIC) {
            if (entry_bytes != ENTRY_LAPIC_BYTES) return false;
            if ((read_u32(entry + 4) & 1u) != 0) {
                if (parsed.enabled_lapics == UINT8_MAX) return false;
                ++parsed.enabled_lapics;
            }
        } else if (type == MADT_IOAPIC) {
            if (entry_bytes != ENTRY_IOAPIC_BYTES || ioapic_seen)
                return false;
            ioapic_seen = true;
            parsed.ioapic_id = entry[2];
            parsed.ioapic_physical = read_u32(entry + 4);
            parsed.ioapic_gsi_base = read_u32(entry + 8);
        } else if (type == MADT_INTERRUPT_OVERRIDE) {
            if (entry_bytes != ENTRY_ISO_BYTES) return false;
            if (entry[2] == ISA_BUS && entry[3] == ISA_IRQ_TIMER) {
                if (irq0_override_seen) return false;
                irq0_override_seen = true;
                parsed.irq0_gsi = read_u32(entry + 4);
                parsed.irq0_flags = read_u16(entry + 8);
                /* ACPI: 00=conforms; 01=active high/edge; 11=low/level.
                 * 10 is reserved for each two-bit polarity/trigger field. */
                if ((parsed.irq0_flags & ~UINT16_C(0x000f)) != 0 ||
                    (parsed.irq0_flags & 3u) == 2u ||
                    ((parsed.irq0_flags >> 2) & 3u) == 2u)
                    return false;
            }
        } else if (type == MADT_LAPIC_OVERRIDE) {
            if (entry_bytes != ENTRY_LAPIC_OVERRIDE_BYTES ||
                lapic_override_seen) return false;
            lapic_override_seen = true;
            parsed.lapic_physical = read_u64(entry + 4);
        }
        offset += entry_bytes;
    }
    if (!ioapic_seen || parsed.ioapic_physical == 0 ||
        parsed.lapic_physical == 0 || parsed.enabled_lapics == 0 ||
        parsed.irq0_gsi < parsed.ioapic_gsi_base)
        return false;
    /* Actual pin count is obtained only from mapped IOAPIC version MMIO.
     * This check prevents claiming a GSI less than this chip's base. */
    *out = parsed;
    return true;
}
