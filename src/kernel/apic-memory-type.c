/* Purpose: conservative x86 APIC MMIO cache/alias authorization.
 * Dependencies: apic-memory-type.h; validated CPU MSR snapshot supplied by
 * boot caller; no device reads, register writes, heap or interrupts.
 * Reference: docs/decisions/ADR-m9-3d5-apic-readonly.md. */
#include "apic-memory-type.h"

/* The known physical HHDM aliases are forbidden unless provably absent.
 * We cannot classify an unreadable table chain as safely unmapped.
 * PAT index3 UC dominates enabled MTRR defaults on x86; the enabled MTRR
 * baseline is still required to reject unexpected cache-control state. */
bool apic_memory_type_safe(const struct apic_memory_type_evidence *e)
{
    if(e==0 || !e->mtrr_cpuid_supported ||
       ((e->ia32_pat>>24)&UINT64_C(0xff))!=0 ||
       e->lapic_hhdm!=X86_PAGE_PROBE_ABSENT ||
       e->ioapic_hhdm!=X86_PAGE_PROBE_ABSENT)
        return false;
    /* MTRRCAP low 8 bits give variable MTRR count; reject surprising
     * platform complexity rather than read unknown-number MSRs. */
    uint64_t count=e->ia32_mtrrcap&UINT64_C(0xff);
    if(count==0 || count>16) return false;
    /* IA32_MTRR_DEF_TYPE bit11 enables MTRRs. Other bits except fixed
     * MTRR enable bit10 and default type [7:0] are reserved and rejected. */
    if((e->ia32_mtrr_def_type&~UINT64_C(0xcff))!=0 ||
       (e->ia32_mtrr_def_type&UINT64_C(0x800))==0)
        return false;
    uint64_t type=e->ia32_mtrr_def_type&UINT64_C(0xff);
    return type==0 || type==1 || type==4 || type==5 ||
           type==6 || type==7;
}
