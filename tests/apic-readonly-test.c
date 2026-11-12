/* Purpose: hosted injected LAPIC/IOAPIC read-only validation tests.
 * Invariants: fake register backing, no privileged hardware, no IRQ writes. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "apic-readonly.h"

struct fake_apic {
    uint32_t lapic_id;
    uint32_t ioapic_id;
    uint32_t version;
    int fail;
    unsigned read_count;
    unsigned selector_count;
};

/* Mock only the LAPIC ID read; enforce no additional LAPIC registers. */
static bool lapic_read(void *context,uint32_t *value)
{
    struct fake_apic *f=context;
    if(f->fail==1 || value==0) return false;
    ++f->read_count;
    *value=f->lapic_id;
    return true;
}

/* Mock a selection-latch write and data read for exactly 0/1 register IDs. */
static bool ioapic_read(void *context,uint8_t index,uint32_t *value)
{
    struct fake_apic *f=context;
    if(index>1 || value==0 || f->fail==2+(int)index) return false;
    ++f->selector_count;
    ++f->read_count;
    *value=index==0?f->ioapic_id:f->version;
    return true;
}

/* Assert output atomicity across all failed topology and register states. */
static void reject(struct acpi_apic_topology *top,struct fake_apic *f,
                   uint8_t id)
{
    struct apic_readonly_backend b={lapic_read,ioapic_read,f};
    struct apic_readonly_result output={0x55,0xaa,0xbb,0xcccc,0x12345678};
    struct apic_readonly_result before=output;
    assert(!apic_readonly_validate(top,id,&b,&output));
    assert(memcmp(&output,&before,sizeof output)==0);
}

/* Verify bounded register sequence and negative ID/version/GSI/IO failures. */
int main(void)
{
    struct acpi_apic_topology top={
        .lapic_physical=UINT64_C(0xfee00000),
        .ioapic_physical=UINT32_C(0xfec00000),
        .ioapic_id=2,.boot_lapic_id=0,.enabled_lapics=1,
        .ioapic_gsi_base=0,.irq0_gsi=2
    };
    struct fake_apic f={.lapic_id=0,.ioapic_id=UINT32_C(2)<<24,
                        .version=(23u<<16)|0x20};
    struct apic_readonly_backend backend={lapic_read,ioapic_read,&f};
    struct apic_readonly_result output={0};
    assert(apic_readonly_validate(&top,0,&backend,&output));
    assert(output.lapic_id==0 && output.ioapic_id==2 &&
           output.redirection_entries==24 && output.irq0_gsi==2);
    assert(f.read_count==3 && f.selector_count==2);
    f.fail=1;reject(&top,&f,0);f.fail=2;reject(&top,&f,0);
    f.fail=3;reject(&top,&f,0);f.fail=0;
    top.boot_lapic_id=1;reject(&top,&f,0);top.boot_lapic_id=0;
    f.lapic_id=UINT32_C(3)<<24;reject(&top,&f,0);f.lapic_id=0;
    f.ioapic_id=UINT32_C(4)<<24;reject(&top,&f,0);
    f.ioapic_id=UINT32_C(2)<<24;
    f.version=0;reject(&top,&f,0);
    f.version=(255u<<16)|0x20;reject(&top,&f,0);
    f.version=(23u<<16)|0x20;
    top.irq0_gsi=24;reject(&top,&f,0);
    return 0;
}
