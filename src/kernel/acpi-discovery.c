/* Purpose: bounded ACPI RSDP/XSDT/MADT discovery with injected mapping.
 * Dependencies: acpi-discovery.h; caller must validate mapped physical span.
 * Invariants: no direct physical pointer construction, no allocation, IO,
 * APIC register access or interrupt enabling; transactional topology output.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#include "acpi-discovery.h"

enum {
    RSDP_OLD_BYTES=20, RSDP_NEW_BYTES=36, SDT_HEADER_BYTES=36,
    MAX_ACPI_BYTES=65536, XSDT_ENTRY_BYTES=8
};

/* Decode fixed-size little-endian values from prevalidated mapped spans. */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Decode a physical table pointer; no dereference follows without mapper. */
static uint64_t get64(const uint8_t *p)
{ return (uint64_t)get32(p) | ((uint64_t)get32(p+4) << 32); }

/* Validate both RSDP checksum sections and full extended structure length
 * before exposing the XSDT physical address. Legacy ACPI 1.0 is rejected. */
bool acpi_rsdp_xsdt_physical(const uint8_t *rsdp, size_t available,
                             uint64_t *xsdt_physical)
{
    if (rsdp == NULL || xsdt_physical == NULL || available < RSDP_NEW_BYTES)
        return false;
    const uint8_t sig[]={'R','S','D',' ','P','T','R',' '};
    for (size_t i=0;i<sizeof sig;++i)
        if (rsdp[i]!=sig[i]) return false;
    if (rsdp[15] < 2 || get32(rsdp+20)!=RSDP_NEW_BYTES ||
        !acpi_bytes_checksum_valid(rsdp,RSDP_OLD_BYTES) ||
        !acpi_bytes_checksum_valid(rsdp,RSDP_NEW_BYTES)) return false;
    uint64_t physical=get64(rsdp+24);
    if (physical==0 || (physical & 7u)!=0) return false;
    *xsdt_physical=physical;
    return true;
}

/* Read a header through the injected mapper, then request exactly the
 * declared table bytes. Never trust an unvalidated length to drive mapping. */
static const uint8_t *validated_sdt(const struct acpi_mapper *mapper,
                                    uint64_t address)
{
    if (address==0 || (address&3u)!=0) return 0;
    const uint8_t *header=mapper->map(mapper->context,address,SDT_HEADER_BYTES);
    if (header==0) return 0;
    uint32_t length=get32(header+4);
    if (length<SDT_HEADER_BYTES || length>MAX_ACPI_BYTES ||
        address>UINT64_MAX-length) return 0;
    const uint8_t *table=mapper->map(mapper->context,address,length);
    if (table==0 || !acpi_bytes_checksum_valid(table,length)) return 0;
    return table;
}

/* Read all XSDT pointers with strict bounds and checksum. Reject duplicate
 * MADTs rather than selecting an arbitrary firmware description. */
bool acpi_discover_madt(const struct acpi_mapper *mapper,
                         uint64_t xsdt_physical,
                         struct acpi_apic_topology *out)
{
    if (mapper==0 || mapper->map==0 || out==0) return false;
    const uint8_t *xsdt=validated_sdt(mapper,xsdt_physical);
    if (xsdt==0 || xsdt[0]!='X'||xsdt[1]!='S'||xsdt[2]!='D'||xsdt[3]!='T')
        return false;
    uint32_t length=get32(xsdt+4);
    if ((length-SDT_HEADER_BYTES)%XSDT_ENTRY_BYTES!=0) return false;
    struct acpi_apic_topology found={0};
    bool seen=false;
    for(size_t i=SDT_HEADER_BYTES;i<length;i+=XSDT_ENTRY_BYTES) {
        uint64_t address=get64(xsdt+i);
        const uint8_t *table=validated_sdt(mapper,address);
        if(table==0) return false;
        if(table[0]=='A'&&table[1]=='P'&&table[2]=='I'&&table[3]=='C') {
            if(seen || !acpi_madt_parse(table,get32(table+4),&found))
                return false;
            seen=true;
        }
    }
    if(!seen) return false;
    *out=found;
    return true;
}
