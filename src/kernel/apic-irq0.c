/* Purpose: minimal testable first IOAPIC IRQ0 routing and LAPIC EOI.
 * Dependencies: apic-irq0.h, injected register backend.
 * Invariants: bounded IOREGSEL/IOWIN transactions, no changes to other pins,
 * no memory allocation, no nested IRQ, one boot CPU and IF=0 for setup.
 * Reference: docs/decisions/ADR-m9-3d6-irq0-delivery.md. */
#include "apic-irq0.h"

enum {
    IOAPIC_SELECT=0, IOAPIC_WINDOW=0x10,
    IOAPIC_REDIR_FIRST=0x10,
    LAPIC_TPR=0x80, LAPIC_EOI=0xb0, LAPIC_SVR=0xf0,
    LAPIC_LINT0=0x350,
    REDIR_MASK=1u<<16, LAPIC_SW_ENABLE=1u<<8
};

/* An indirect register read alters only the IOREGSEL latch and restores its
 * original selector. Each IO operation is checked; caller owns backend. */
static bool ioapic_read(const struct apic_irq0_io *io,uint8_t index,
                        uint32_t *value)
{
    uint32_t old;
    if(!io->read(io->context,false,IOAPIC_SELECT,&old)) return false;
    if(!io->write(io->context,false,IOAPIC_SELECT,index)) return false;
    bool ok=io->read(io->context,false,IOAPIC_WINDOW,value);
    if(!io->write(io->context,false,IOAPIC_SELECT,old)) return false;
    return ok;
}

/* The high or low redirection register is changed through one index-latch
 * selection, then the previous selection restored. Failures are unsafe
 * until a separate rollback proves the old configuration. */
static bool ioapic_write(const struct apic_irq0_io *io,uint8_t index,
                         uint32_t value)
{
    uint32_t old;
    if(!io->read(io->context,false,IOAPIC_SELECT,&old) ||
       !io->write(io->context,false,IOAPIC_SELECT,index))
        return false;
    bool ok=io->write(io->context,false,IOAPIC_WINDOW,value);
    if(!io->write(io->context,false,IOAPIC_SELECT,old)) return false;
    return ok;
}

/* Reject ambiguous physical topology and any existing active IOAPIC entry;
 * a registered timer is not permission to interrupt another driver. */
bool apic_irq0_probe(const struct apic_irq0_io *io,
                     const struct acpi_apic_topology *topology,
                     uint8_t cpu_apic_id,uint16_t count,
                     struct apic_irq0_route *out)
{
    if(io==0 || io->read==0 || io->write==0 || topology==0 || out==0 ||
       topology->enabled_lapics!=1 ||
       topology->boot_lapic_id!=cpu_apic_id ||
       topology->ioapic_id>15 || count<2 || count>120 ||
       topology->irq0_gsi<topology->ioapic_gsi_base ||
       topology->irq0_gsi-topology->ioapic_gsi_base>=count ||
       (topology->irq0_flags&~UINT16_C(0x000f))!=0 ||
       ((topology->irq0_flags&3u)>1u) ||
       (((topology->irq0_flags>>2)&3u)>1u))
        return false;
    uint32_t tpr=0,lint0=0,svr=0;
    if(!io->read(io->context,true,LAPIC_TPR,&tpr) || tpr!=0 ||
       !io->read(io->context,true,LAPIC_LINT0,&lint0) ||
       (lint0&UINT32_C(0x10000))==0 ||
       !io->read(io->context,true,LAPIC_SVR,&svr))
        return false;
    uint32_t selected=topology->irq0_gsi-topology->ioapic_gsi_base;
    uint32_t low=0,high=0;
    for(uint32_t pin=0;pin<count;++pin) {
        uint8_t register_low=(uint8_t)(IOAPIC_REDIR_FIRST+2*pin);
        uint32_t candidate=0;
        if(!ioapic_read(io,register_low,&candidate) ||
           (candidate&REDIR_MASK)==0)
            return false;
        if(pin==selected) {
            low=candidate;
            if(!ioapic_read(io,(uint8_t)(register_low+1),&high))
                return false;
        }
    }
    /* Reject active delivery/remote-IRR in the timer's own slot. Preserve
     * only the masked state; all new redirection fields are explicit. */
    if((low&UINT32_C(0x4000))!=0) return false;
    *out=(struct apic_irq0_route){
        .irq0_pin=(uint8_t)selected,.destination_apic_id=cpu_apic_id,
        .saved_redirection_low=low,.saved_redirection_high=high,
        .saved_svr=svr,.prepared=false,.unmasked=false,.poisoned=false
    };
    return true;
}

/* Stage fully masked timer route and LAPIC SVR. The source remains masked
 * after this function; even a partial write cannot deliver PIT0. */
bool apic_irq0_prepare(const struct apic_irq0_io *io,
                       struct apic_irq0_route *route)
{
    if(io==0 || io->read==0 || io->write==0 || route==0 ||
       route->prepared || route->unmasked || route->poisoned)
        return false;
    uint8_t low_index=(uint8_t)(IOAPIC_REDIR_FIRST+2u*route->irq0_pin);
    uint32_t high=((uint32_t)route->destination_apic_id)<<24;
    uint32_t masked=REDIR_MASK | APIC_IRQ0_VECTOR;
    /* Poison immediately before first write, then clear only after every
     * readback; on failure no STI and explicit recovery is mandatory. */
    route->poisoned=true;
    uint32_t actual;
    if(!ioapic_write(io,(uint8_t)(low_index+1),high) ||
       !ioapic_write(io,low_index,masked) ||
       !ioapic_read(io,(uint8_t)(low_index+1),&actual) || actual!=high ||
       !ioapic_read(io,low_index,&actual) || actual!=masked ||
       !io->write(io->context,true,LAPIC_SVR,
                  (route->saved_svr & ~UINT32_C(0xff)) |
                    APIC_SPURIOUS_VECTOR | LAPIC_SW_ENABLE) ||
       !io->read(io->context,true,LAPIC_SVR,&actual) ||
       (actual&UINT32_C(0x1ff))!=UINT32_C(0x1ff))
        return false;
    route->prepared=true;
    route->poisoned=false;
    return true;
}

/* Unmask only the verified timer pin, preserving destination and vector.
 * A failed readback is fatal with IF=0; the kernel must not claim success. */
bool apic_irq0_unmask(const struct apic_irq0_io *io,
                      struct apic_irq0_route *route)
{
    if(io==0 || io->read==0 || io->write==0 || route==0 ||
       !route->prepared || route->unmasked || route->poisoned)
        return false;
    uint8_t low=(uint8_t)(IOAPIC_REDIR_FIRST+2u*route->irq0_pin);
    uint32_t actual;
    route->poisoned=true;
    if(!ioapic_write(io,low,APIC_IRQ0_VECTOR) ||
       !ioapic_read(io,low,&actual) || actual!=APIC_IRQ0_VECTOR)
        return false;
    route->unmasked=true;
    route->poisoned=false;
    return true;
}

/* EOI is the only LAPIC write from IRQ context; never acknowledge via PIC. */
bool apic_irq0_eoi(const struct apic_irq0_io *io)
{
    return io!=0 && io->write!=0 &&
           io->write(io->context,true,LAPIC_EOI,0);
}
