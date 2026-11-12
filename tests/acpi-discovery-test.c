/* Purpose: host-only RSDP/XSDT lookup with a controlled physical mapper.
 * Invariants: no hardware, allocations or direct firmware dereferences;
 * cover length, signature, checksum, address and missing MADT errors.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "acpi-discovery.h"

static uint8_t tables[2048];

/* Encode U32 into a simulated firmware byte sequence. */
static void put32(uint8_t *p, uint32_t n)
{ for(size_t i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }

/* Encode a simulated physical pointer in little endian. */
static void put64(uint8_t *p, uint64_t n)
{ put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32)); }

/* Correct a single ACPI checksum field at known offset. */
static void fix(uint8_t *p,size_t len,size_t field)
{
    p[field]=0;uint8_t sum=0;
    for(size_t i=0;i<len;i++)sum=(uint8_t)(sum+p[i]);
    p[field]=(uint8_t)(0-sum);
}

/* Map only the mock address range, with full address+length proof. */
static const uint8_t *mock_map(void *context, uint64_t phys, size_t length)
{
    (void)context;
    if(phys>=sizeof tables||length>sizeof tables-phys) return NULL;
    return tables+(size_t)phys;
}

/* Compose valid RSDP/XSDT/MADT byte objects at mock physical offsets. */
static void build_fixture(void)
{
    memset(tables,0,sizeof tables);
    uint8_t *rsdp=tables+0x100;
    memcpy(rsdp,"RSD PTR ",8);rsdp[15]=2;put32(rsdp+20,36);
    put64(rsdp+24,0x200);fix(rsdp,20,8);fix(rsdp,36,32);
    uint8_t *xsdt=tables+0x200;
    memcpy(xsdt,"XSDT",4);put32(xsdt+4,44);xsdt[8]=1;
    put64(xsdt+36,0x300);fix(xsdt,44,9);
    uint8_t *madt=tables+0x300;
    memcpy(madt,"APIC",4);put32(madt+4,64);madt[8]=3;
    put32(madt+36,0xfee00000u);
    madt[44]=0;madt[45]=8;madt[47]=1;put32(madt+48,1);
    madt[52]=1;madt[53]=12;madt[54]=2;
    put32(madt+56,0xfec00000u);put32(madt+60,0u);
    fix(madt,64,9);
}

/* Validate correct chain and each independent rejection condition. */
int main(void)
{
    build_fixture();
    struct acpi_mapper mapper={mock_map,NULL};
    uint64_t xsdt=UINT64_C(0x8888);
    struct acpi_apic_topology out={0};
    assert(acpi_rsdp_xsdt_physical(tables+0x100,36,&xsdt));
    assert(xsdt==0x200);
    assert(acpi_discover_madt(&mapper,xsdt,&out));
    assert(out.lapic_physical==0xfee00000u);
    assert(out.ioapic_physical==0xfec00000u);
    assert(out.irq0_gsi==0);
    assert(!acpi_rsdp_xsdt_physical(tables+0x100,19,&xsdt));
    assert(xsdt==0x200);
    tables[0x100]^=1;
    assert(!acpi_rsdp_xsdt_physical(tables+0x100,36,&xsdt));
    build_fixture();
    tables[0x100+32]^=1;
    assert(!acpi_rsdp_xsdt_physical(tables+0x100,36,&xsdt));
    build_fixture();
    tables[0x200+9]^=1;
    assert(!acpi_discover_madt(&mapper,0x200,&out));
    build_fixture();
    put64(tables+0x200+36,UINT64_MAX-2);
    fix(tables+0x200,44,9);
    assert(!acpi_discover_madt(&mapper,0x200,&out));
    build_fixture();
    memcpy(tables+0x300,"TEST",4);fix(tables+0x300,64,9);
    assert(!acpi_discover_madt(&mapper,0x200,&out));
    build_fixture();
    put32(tables+0x300+4,65537);
    assert(!acpi_discover_madt(&mapper,0x200,&out));
    build_fixture();
    assert(!acpi_discover_madt(NULL,0x200,&out));
    assert(!acpi_discover_madt(&mapper,0x200,NULL));
    return 0;
}
