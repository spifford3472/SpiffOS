/* Purpose: validate ACPI RSDP and XSDT before MADT topology processing.
 * Dependencies: acpi-madt.h and borrowed caller-provided physical mapper.
 * Invariants: no arbitrary firmware address dereference, no heap, no MMIO;
 * output is immutable on failure, all table reads bounded by mapping callback.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#ifndef ACPI_DISCOVERY_H
#define ACPI_DISCOVERY_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "acpi-madt.h"

/* Mapper returns a pointer to at least bytes verified readable at physical
 * address, or NULL. It must validate physical range, mapping lifetime, and
 * any page boundaries; this parser NEVER constructs a pointer from a GSI. */
typedef const uint8_t *(*acpi_map_physical)(void *context,
                                            uint64_t physical,
                                            size_t bytes);
struct acpi_mapper {
    acpi_map_physical map;
    void *context;
};

/* Parse RSDP 2+ with both standard and extended checksum. Caller supplies
 * a fully mapped RSDP span of at least 36 bytes; no physical mapping occurs. */
bool acpi_rsdp_xsdt_physical(const uint8_t *rsdp, size_t available,
                             uint64_t *xsdt_physical);

/* Find MADT from a validated XSDT using bounded physical mapping and
 * checksums. No MMIO registers are mapped or accessed. Fail if multiple MADTs
 * or malformed entries; leave caller output unchanged on all errors. */
bool acpi_discover_madt(const struct acpi_mapper *mapper,
                         uint64_t xsdt_physical,
                         struct acpi_apic_topology *out);
#endif
