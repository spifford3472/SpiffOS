/* Purpose: hosted PAT/MTRR and alias safety negative tests.
 * Dependencies: pure apic-memory-type.c, no real MSR or APIC MMIO. */
#include <assert.h>
#include <stdint.h>
#include "apic-memory-type.h"

/* Check conservative UC policy and all ambiguous alias failure modes. */
int main(void)
{
    struct apic_memory_type_evidence e={
        .mtrr_cpuid_supported=true,
        .ia32_mtrrcap=8,
        .ia32_mtrr_def_type=UINT64_C(0xc06),
        .ia32_pat=UINT64_C(0x0007040600070406),
        .lapic_hhdm=X86_PAGE_PROBE_ABSENT,
        .ioapic_hhdm=X86_PAGE_PROBE_ABSENT
    };
    assert(apic_memory_type_safe(&e));
    assert(!apic_memory_type_safe(NULL));
    e.lapic_hhdm=X86_PAGE_PROBE_UNKNOWN;
    assert(!apic_memory_type_safe(&e));
    e.lapic_hhdm=X86_PAGE_PROBE_ABSENT;
    e.ioapic_hhdm=X86_PAGE_PROBE_PRESENT;
    assert(!apic_memory_type_safe(&e));
    e.ioapic_hhdm=X86_PAGE_PROBE_ABSENT;
    e.mtrr_cpuid_supported=false;
    assert(!apic_memory_type_safe(&e));
    e.mtrr_cpuid_supported=true;
    e.ia32_pat|=UINT64_C(6)<<24;
    assert(!apic_memory_type_safe(&e));
    e.ia32_pat=UINT64_C(0x0007040600070406);
    e.ia32_mtrrcap=17;
    assert(!apic_memory_type_safe(&e));
    e.ia32_mtrrcap=0;
    assert(!apic_memory_type_safe(&e));
    e.ia32_mtrrcap=8;
    e.ia32_mtrr_def_type=UINT64_C(0x406);
    assert(!apic_memory_type_safe(&e));
    e.ia32_mtrr_def_type=UINT64_C(0xc02);
    assert(!apic_memory_type_safe(&e));
    e.ia32_mtrr_def_type=UINT64_C(0xc06)|UINT64_C(0x1000);
    assert(!apic_memory_type_safe(&e));
    return 0;
}
