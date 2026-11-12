/* Purpose: minimal, bounded boot-CPU APIC aperture page-table transaction.
 * Dependencies: x86-mmio-map.h and a trusted exact-owner backend.
 * Invariants: no hardware MMIO, page-table allocation is caller-owned,
 * no writes to existing present mappings, root entry linked last.
 * Reference: docs/decisions/ADR-m9-3d4-mmio-mapping.md. */
#include "x86-mmio-map.h"

enum { PAGE_BYTES=4096, ENTRIES=512 };
static const uint64_t TABLE_ENTRY_FLAGS=UINT64_C(3);
static const uint64_t TABLE_ADDRESS_MASK=UINT64_C(0x000ffffffffff000);

/* Decode one fixed-level index in the 4/5-level page tree. The two
 * aperture pages differ only at the final PT entry, so all parent indices
 * are identical. level 1 denotes PT and level 5 denotes PML5. */
static size_t page_index(uint64_t address, uint8_t level)
{ return (size_t)((address >> (12u+9u*(level-1u))) & 511u); }

/* Revoke any partial page acquisition; a release failure permanently
 * poisons the transaction instead of allowing reallocation of linked RAM. */
static bool release_staged(struct x86_mmio_mapping *state,
                           const struct x86_mmio_map_backend *backend)
{
    bool ok=true;
    while(state->table_count>0) {
        size_t i=--state->table_count;
        if(!backend->release(backend->context,state->physical[i]))
            ok=false;
        state->physical[i]=0;
        state->writable[i]=0;
    }
    if(!ok) state->poisoned=true;
    return ok;
}

/* Publish complete privately assembled tables through exactly one root
 * entry after verifying the selected high-half slot is vacant. On any
 * acquire failure, free only completely unlinked pages; the root is never
 * published partially. A failed release poisons state and requires halt. */
bool x86_mmio_map_create(struct x86_mmio_mapping *state,
    uint8_t levels, uint64_t *root, uint64_t lapic_physical,
    uint64_t ioapic_physical, const struct mmio_page_cpu_policy *policy,
    const struct x86_mmio_map_backend *backend)
{
    if(state==0 || state->active || state->poisoned || root==0 ||
       backend==0 || backend->acquire==0 || backend->release==0 ||
       backend->invalidate==0 || policy==0 ||
       (levels!=4 && levels!=5) || ((uintptr_t)root & (PAGE_BYTES-1u))!=0)
        return false;
    /* Both PTEs are preflighted before the first allocation. This prevents
     * partial state when one controller physical address is unsupported. */
    uint64_t lapic_entry,ioapic_entry;
    if(mmio_page_plan_leaf(MMIO_APIC_LAPIC_VIRTUAL,lapic_physical,0,
                           policy,&lapic_entry)!=MMIO_PAGE_OK ||
       mmio_page_plan_leaf(MMIO_APIC_IOAPIC_VIRTUAL,ioapic_physical,0,
                           policy,&ioapic_entry)!=MMIO_PAGE_OK ||
       lapic_physical==ioapic_physical)
        return false;
    size_t root_slot=page_index(MMIO_APIC_LAPIC_VIRTUAL,levels);
    if(root[root_slot]!=0) return false;
    size_t count=(size_t)levels-1;
    for(size_t i=0;i<count;++i) {
        uint64_t phys=0;
        uint64_t *mapped=0;
        if(!backend->acquire(backend->context,&phys,&mapped)) {
            (void)release_staged(state,backend);
            return false;
        }
        /* Require a completely zeroed owned 4KiB page so no stale PTE is
         * accidentally linked. Reject page 0, 52-bit overflow and duplicates. */
        bool valid=phys!=0 && (phys&(PAGE_BYTES-1u))==0 &&
                   (phys&~TABLE_ADDRESS_MASK)==0 && mapped!=0 &&
                   (((uintptr_t)mapped)&(PAGE_BYTES-1u))==0;
        for(size_t j=0;j<i;++j)
            if(phys==state->physical[j]) valid=false;
        if(valid) for(size_t j=0;j<ENTRIES;++j)
            if(mapped[j]!=0) {valid=false;break;}
        if(!valid) {
            if(!backend->release(backend->context,phys))
                state->poisoned=true;
            (void)release_staged(state,backend);
            return false;
        }
        state->physical[i]=phys;
        state->writable[i]=mapped;
        ++state->table_count;
    }
    /* Table[0] is directly beneath CR3; table[count-1] is the leaf PT.
     * Set all lower parent links before the single root commit. */
    uint64_t *pt=state->writable[count-1];
    pt[page_index(MMIO_APIC_LAPIC_VIRTUAL,1)]=lapic_entry;
    pt[page_index(MMIO_APIC_IOAPIC_VIRTUAL,1)]=ioapic_entry;
    for(size_t i=count-1;i>0;--i) {
        uint8_t parent_level=(uint8_t)(levels-i);
        size_t slot=page_index(MMIO_APIC_LAPIC_VIRTUAL,parent_level);
        state->writable[i-1][slot]=state->physical[i]|TABLE_ENTRY_FLAGS;
    }
    /* On x86-64 aligned 64-bit root PTE stores are atomic. The compiler
     * barrier prevents staging writes moving after publication; IF remains 0. */
    __asm__ volatile ("" : : : "memory");
    root[root_slot]=state->physical[0]|TABLE_ENTRY_FLAGS;
    __asm__ volatile ("" : : : "memory");
    state->root_entry=&root[root_slot];
    state->active=true;
    backend->invalidate(backend->context,MMIO_APIC_LAPIC_VIRTUAL);
    backend->invalidate(backend->context,MMIO_APIC_IOAPIC_VIRTUAL);
    return true;
}

/* Clear only the root link this transaction installed, with no other
 * references to the private tables. Invalidate both TLB entries before
 * returning table frames; failed ownership release permanently poisons. */
bool x86_mmio_map_destroy(struct x86_mmio_mapping *state,
    const struct x86_mmio_map_backend *backend)
{
    if(state==0 || !state->active || state->poisoned ||
       backend==0 || backend->release==0 || backend->invalidate==0 ||
       state->root_entry==0 || state->table_count==0)
       return false;
    if(*state->root_entry!=(state->physical[0]|TABLE_ENTRY_FLAGS)) {
        state->poisoned=true;
        return false;
    }
    *state->root_entry=0;
    __asm__ volatile ("" : : : "memory");
    backend->invalidate(backend->context,MMIO_APIC_LAPIC_VIRTUAL);
    backend->invalidate(backend->context,MMIO_APIC_IOAPIC_VIRTUAL);
    state->active=false;
    state->root_entry=0;
    return release_staged(state,backend);
}
