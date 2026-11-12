/* Purpose: fake-MMIO tests for one IRQ0 routing transaction.
 * Invariants: no real APIC, PIT, STI, IRQ handler or physical memory.
 * Reference: docs/decisions/ADR-m9-3d6-irq0-delivery.md. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "apic-irq0.h"

struct fake_controller {
    uint32_t lapic[1024];
    uint32_t ioapic[256];
    uint32_t selected;
    unsigned config_writes, eoi_writes;
    bool fail_write;
};

/* Read mock local register or indirect IOAPIC index/data window. */
static bool fake_read(void *ctx,bool local,uint32_t offset,uint32_t *out)
{
    struct fake_controller *f=ctx;
    if(f==0 || out==0) return false;
    if(local) {
        if(offset%4!=0 || offset/4>=1024) return false;
        *out=f->lapic[offset/4];
    } else if(offset==0) *out=f->selected;
    else if(offset==0x10) *out=f->ioapic[f->selected&255u];
    else return false;
    return true;
}

/* Count only writes to actual redirection or local APIC registers, not
 * register selection itself. Inject partial write rejection as negative. */
static bool fake_write(void *ctx,bool local,uint32_t offset,uint32_t value)
{
    struct fake_controller *f=ctx;
    if(f==0 || f->fail_write) return false;
    if(local) {
        if(offset%4!=0 || offset/4>=1024) return false;
        if(offset==0xb0) ++f->eoi_writes;
        else ++f->config_writes;
        f->lapic[offset/4]=value;
    } else if(offset==0) f->selected=value;
    else if(offset==0x10) {
        f->ioapic[f->selected&255u]=value;
        ++f->config_writes;
    } else return false;
    return true;
}

/* Populate a QEMU-like topology with every non-timer pin masked. */
static void setup(struct fake_controller *f)
{
    *f=(struct fake_controller){0};
    for(unsigned i=0;i<24;i++)
        f->ioapic[0x10+2*i]=UINT32_C(0x10000);
    f->lapic[0x350/4]=UINT32_C(0x10000);
    f->lapic[0xf0/4]=UINT32_C(0xff);
}

/* Prove no configuration mutation during preflight, only chosen pin
 * unmasked, correct APIC ID/vector, and EOI from callback. */
int main(void)
{
    struct fake_controller f;
    setup(&f);
    struct apic_irq0_io io={fake_read,fake_write,&f};
    struct acpi_apic_topology topology={
        .enabled_lapics=1,.boot_lapic_id=0,.ioapic_id=2,
        .irq0_gsi=2,.ioapic_gsi_base=0,.irq0_flags=0
    };
    struct apic_irq0_route route={0};
    assert(apic_irq0_probe(&io,&topology,0,24,&route));
    assert(f.config_writes==0 && route.irq0_pin==2);
    assert(apic_irq0_prepare(&io,&route));
    assert(route.prepared && !route.unmasked);
    assert(f.ioapic[0x10+4]==UINT32_C(0x10030));
    assert((f.lapic[0xf0/4]&0x1ff)==0x1ff);
    assert(apic_irq0_unmask(&io,&route));
    assert(route.unmasked && f.ioapic[0x10+4]==0x30);
    assert(f.ioapic[0x10+2*1]==UINT32_C(0x10000));
    assert(apic_irq0_eoi(&io) && f.eoi_writes==1);
    assert(!apic_irq0_unmask(&io,&route));
    setup(&f);
    f.ioapic[0x10+2*3]=UINT32_C(0x30);
    route=(struct apic_irq0_route){0};
    assert(!apic_irq0_probe(&io,&topology,0,24,&route));
    assert(f.config_writes==0);
    setup(&f);
    topology.irq0_flags=0xf;
    assert(!apic_irq0_probe(&io,&topology,0,24,&route));
    topology.irq0_flags=0;
    topology.irq0_gsi=24;
    assert(!apic_irq0_probe(&io,&topology,0,24,&route));
    topology.irq0_gsi=2;
    f.lapic[0x350/4]=0; /* unmasked ExtINT must fail closed */
    assert(!apic_irq0_probe(&io,&topology,0,24,&route));
    setup(&f);
    assert(apic_irq0_probe(&io,&topology,0,24,&route));
    f.fail_write=true;
    assert(!apic_irq0_prepare(&io,&route));
    assert(route.poisoned && !route.prepared);
    return 0;
}
