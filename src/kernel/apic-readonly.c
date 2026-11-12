/* Purpose: pure validation sequence for bounded APIC ID/version reads.
 * Dependencies: apic-readonly.h; caller injects a separately validated
 * MMIO backend and owns all state, boot CPU IF=0.
 * Invariants: no LAPIC/IOAPIC redirection writes, EOI or interrupt enabling.
 * Reference: docs/decisions/ADR-m9-3d5-apic-readonly.md. */
#include "apic-readonly.h"

enum { IOAPIC_ID_REG=0, IOAPIC_VERSION_REG=1, IOAPIC_MAX_ENTRIES=240 };

/* The backend is called only for the documented safe read registers, once
 * each; on partial failure no result is published. IOREGSEL writes are
 * implementation details of the injected index-read backend, not this API. */
bool apic_readonly_validate(const struct acpi_apic_topology *topology,
                            uint8_t cpuid_lapic_id,
                            const struct apic_readonly_backend *backend,
                            struct apic_readonly_result *out)
{
    if(topology==0 || backend==0 || out==0 ||
       backend->lapic_id_read==0 || backend->ioapic_index_read==0 ||
       topology->enabled_lapics!=1 ||
       topology->boot_lapic_id!=cpuid_lapic_id ||
       topology->ioapic_id>15 ||
       topology->irq0_gsi<topology->ioapic_gsi_base)
        return false;
    uint32_t lapic_raw=0;
    if(!backend->lapic_id_read(backend->context,&lapic_raw))
        return false;
    uint8_t lapic_id=(uint8_t)(lapic_raw>>24);
    if(lapic_id!=cpuid_lapic_id || (lapic_raw&UINT32_C(0xffffff))!=0)
        return false;
    uint32_t ioapic_id_raw=0;
    if(!backend->ioapic_index_read(backend->context,IOAPIC_ID_REG,
                                   &ioapic_id_raw))
        return false;
    uint8_t ioapic_id=(uint8_t)((ioapic_id_raw>>24)&15u);
    if(ioapic_id!=topology->ioapic_id ||
       (ioapic_id_raw&UINT32_C(0xf0ffffff))!=0)
        return false;
    uint32_t ioapic_version_raw=0;
    if(!backend->ioapic_index_read(backend->context,IOAPIC_VERSION_REG,
                                   &ioapic_version_raw))
        return false;
    uint32_t maximum_index=(ioapic_version_raw>>16)&255u;
    uint32_t version=ioapic_version_raw&255u;
    uint32_t count=maximum_index+1u;
    if(version==0 || version==255 || count<2 ||
       count>IOAPIC_MAX_ENTRIES ||
       (ioapic_version_raw&UINT32_C(0xff000000))!=0 ||
       topology->ioapic_gsi_base>UINT32_MAX-count ||
       topology->irq0_gsi>=topology->ioapic_gsi_base+count)
        return false;
    struct apic_readonly_result result={
        .lapic_id=lapic_id,
        .ioapic_id=ioapic_id,
        .ioapic_version=(uint8_t)version,
        .redirection_entries=(uint16_t)count,
        .irq0_gsi=topology->irq0_gsi
    };
    *out=result;
    return true;
}
