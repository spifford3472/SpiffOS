/* Purpose: reject unsupported CPU APIC state before any controller MMIO.
 * Dependencies: apic-preflight.h and standard CPUID/RDMSR instructions.
 * Invariants: boot CPU and IF=0; no memory mapping, IO register access,
 * interrupt delivery, heap or allocation. Hardware checks only reject.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#include "apic-preflight.h"

enum {
    APIC_CAPABILITY_BIT = 1u << 9, APIC_MSR = 0x1b,
    APIC_MSR_BSP = 1u << 8, APIC_MSR_X2APIC = 1u << 10,
    APIC_MSR_ENABLE = 1u << 11
};

/* Compare verified MADT physical addresses with CPUID/MSR state without
 * touching those addresses. Require xAPIC enabled, x2APIC disabled, BSP
 * ownership, page-aligned LAPIC and IOAPIC and sensible ISA timer GSI. */
bool apic_cpu_topology_preflight(const struct acpi_apic_topology *t,
                                 const struct apic_cpu_snapshot *cpu)
{
    if (t == 0 || cpu == 0 || (cpu->cpuid_leaf1_edx & APIC_CAPABILITY_BIT) == 0 ||
        (cpu->ia32_apic_base_msr & APIC_MSR_ENABLE) == 0 ||
        (cpu->ia32_apic_base_msr & APIC_MSR_BSP) == 0 ||
        (cpu->ia32_apic_base_msr & APIC_MSR_X2APIC) != 0 ||
        (t->lapic_physical & 4095u) != 0 ||
        (t->ioapic_physical & 4095u) != 0 ||
        t->lapic_physical == 0 || t->ioapic_physical == 0 ||
        t->enabled_lapics != 1 ||
        cpu->cpuid_apic_id != t->boot_lapic_id ||
        t->irq0_gsi < t->ioapic_gsi_base)
        return false;
    /* APICBASE[51:12] gives the physical MMIO base in xAPIC mode.
     * Bits above 51 are reserved in this model. */
    uint64_t msr_base = cpu->ia32_apic_base_msr &
                        UINT64_C(0x000ffffffffff000);
    return t->lapic_physical == msr_base;
}

/* CPUID leaf 1 support and APIC presence are checked before the standard
 * MSR access. No model-specific controller writes or interrupts occur. */
bool apic_cpu_read_snapshot(struct apic_cpu_snapshot *out)
{
    if (out == 0) return false;
    uint32_t a=1,b,c,d;
    __asm__ volatile ("cpuid" : "+a"(a),"=b"(b),"=c"(c),"=d"(d));
    if ((d & APIC_CAPABILITY_BIT)==0) return false;
    uint32_t lo,hi;
    __asm__ volatile ("rdmsr" : "=a"(lo),"=d"(hi) : "c"(APIC_MSR));
    struct apic_cpu_snapshot snapshot={
        .cpuid_leaf1_edx=d,
        .ia32_apic_base_msr=((uint64_t)hi << 32)|lo,
        .cpuid_apic_id=(uint8_t)(b>>24)
    };
    *out=snapshot;
    return true;
}
