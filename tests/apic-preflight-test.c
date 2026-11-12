/* Purpose: host-only CPU/MADT compatibility rejection tests.
 * No CPUID, MSR, MMIO or IRQ instructions are executed by test. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "apic-preflight.h"

/* Reject missing APIC, disabled or x2APIC state, misaligned and conflicting
 * physical bases while preserving all caller-owned input objects. */
int main(void)
{
    struct acpi_apic_topology t = {
        .lapic_physical=UINT64_C(0xfee00000),
        .ioapic_physical=0xfec00000u,
        .enabled_lapics=1, .irq0_gsi=2, .ioapic_gsi_base=0
    };
    struct apic_cpu_snapshot cpu = {
        .cpuid_leaf1_edx=(1u<<9),
        .ia32_apic_base_msr=UINT64_C(0xfee00900)
    };
    assert(apic_cpu_topology_preflight(&t,&cpu));
    assert(!apic_cpu_topology_preflight(NULL,&cpu));
    assert(!apic_cpu_topology_preflight(&t,NULL));
    cpu.cpuid_leaf1_edx=0;
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    cpu.cpuid_leaf1_edx=1u<<9;
    cpu.ia32_apic_base_msr &= ~UINT64_C(0x800);
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    cpu.ia32_apic_base_msr |= UINT64_C(0x800);
    cpu.ia32_apic_base_msr |= UINT64_C(0x400);
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    cpu.ia32_apic_base_msr &= ~UINT64_C(0x400);
    cpu.ia32_apic_base_msr &= ~UINT64_C(0x100);
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    cpu.ia32_apic_base_msr |= UINT64_C(0x100);
    t.lapic_physical += 4096;
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    t.lapic_physical -= 4096;
    t.ioapic_physical++;
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    t.ioapic_physical--;
    t.enabled_lapics=0;
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    t.enabled_lapics=1;
    t.ioapic_gsi_base=3;
    assert(!apic_cpu_topology_preflight(&t,&cpu));
    return 0;
}
