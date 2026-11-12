/* Purpose: hosted malicious/valid MADT parser tests.
 * Dependencies: acpi-madt.c pure parser; no mapping, MMIO or device access.
 * Invariants: all caller-owned bytes; catches rejection without output writes.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "acpi-madt.h"

/* Encode a U32 into known-good in-memory ACPI little-endian table bytes. */
static void put32(uint8_t *p, uint32_t n)
{
    for (size_t i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8));
}

/* Recompute checksum after changes, to distinguish syntax from corruption. */
static void fix_checksum(uint8_t *t, size_t n)
{
    t[9] = 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum = (uint8_t)(sum + t[i]);
    t[9] = (uint8_t)(0 - sum);
}

/* Build a table containing exactly one LAPIC, IOAPIC and timer override. */
static size_t valid_table(uint8_t *t)
{
    memset(t, 0, 128);
    memcpy(t, "APIC", 4);
    t[8] = 3; /* SDT revision */
    put32(t + 36, 0xfee00000u);
    size_t p = 44;
    t[p] = 0; t[p+1] = 8; t[p+2] = 0; t[p+3] = 1;
    put32(t+p+4, 1u);
    p += 8;
    t[p] = 1; t[p+1] = 12; t[p+2] = 3;
    put32(t+p+4, 0xfec00000u);
    put32(t+p+8, 0u);
    p += 12;
    t[p] = 2; t[p+1] = 10; t[p+2] = 0; t[p+3] = 0;
    put32(t+p+4, 2u); /* IRQ0 -> GSI2 */
    p += 10;
    put32(t+4, (uint32_t)p);
    fix_checksum(t, p);
    return p;
}

/* Reject bad input transactionally and preserve the output sentinel. */
static void must_reject(uint8_t *t, size_t available, struct acpi_apic_topology *out)
{
    struct acpi_apic_topology original = *out;
    assert(!acpi_madt_parse(t, available, out));
    assert(memcmp(out, &original, sizeof original) == 0);
}

/* Exercise corruption, truncation, reserved flags and duplicate records. */
int main(void)
{
    uint8_t t[128];
    size_t n = valid_table(t);
    struct acpi_apic_topology out = {0};
    assert(acpi_madt_parse(t,n,&out));
    assert(out.lapic_physical == 0xfee00000u);
    assert(out.ioapic_physical == 0xfec00000u);
    assert(out.ioapic_id == 3 && out.enabled_lapics == 1);
    assert(out.boot_lapic_id == 1);
    assert(out.ioapic_gsi_base == 0 && out.irq0_gsi == 2);
    assert(!acpi_madt_parse(NULL,n,&out));
    assert(!acpi_madt_parse(t,n,NULL));
    must_reject(t, n-1, &out);
    t[0] = 'X'; fix_checksum(t,n); must_reject(t,n,&out);
    t[0] = 'A'; fix_checksum(t,n);
    t[9] ^= 1; must_reject(t,n,&out); fix_checksum(t,n);
    put32(t+4, 65537u); must_reject(t,n,&out);
    put32(t+4, (uint32_t)n);
    t[45] = 0; fix_checksum(t,n); must_reject(t,n,&out);
    t[45] = 8; fix_checksum(t,n);
    t[48] = 0; fix_checksum(t,n); must_reject(t,n,&out);
    put32(t+48,1u); fix_checksum(t,n);
    t[52] = 1; t[53] = 11; fix_checksum(t,n); must_reject(t,n,&out);
    t[53] = 12; fix_checksum(t,n);
    t[64] = 2; t[65] = 10; t[72] = 2; fix_checksum(t,n);
    must_reject(t,n,&out); /* reserved polarity */
    t[72] = 0; fix_checksum(t,n);
    assert(acpi_madt_parse(t,n,&out));
    /* Duplicate IOAPIC, even with a different ID, is not yet supported. */
    t[n] = 1; t[n+1] = 12; t[n+2] = 7;
    put32(t+n+4,0xfec01000u); put32(t+n+8,24);
    put32(t+4,(uint32_t)n+12); fix_checksum(t,n+12);
    must_reject(t,n+12,&out);
    n=valid_table(t);
    /* A zero-length or truncated variable record cannot advance safely. */
    t[53]=1; fix_checksum(t,n); must_reject(t,n,&out);
    n=valid_table(t);
    t[53]=40; fix_checksum(t,n); must_reject(t,n,&out);
    assert(!acpi_bytes_checksum_valid(NULL,20));
    assert(!acpi_bytes_checksum_valid(t,0));
    assert(acpi_bytes_checksum_valid(t,n));
    return 0;
}
