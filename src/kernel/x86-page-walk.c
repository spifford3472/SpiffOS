/* Purpose: conservative read-only x86-64 4/5-level page-table walker.
 * Dependencies: x86-page-walk.h, 4KiB table pages supplied by safe mapper.
 * Invariants: no arbitrary PTE pointer dereference, MMIO, page mutation,
 * resource acquisition or interrupt enabling; failure leaves output intact.
 * Reference: docs/decisions/ADR-m9-3-apic-routing.md. */
#include "x86-page-walk.h"

enum {
    TABLE_BYTES=4096, ENTRY_BYTES=8,
    BIT_PRESENT=1, BIT_WRITABLE=2, BIT_USER=4, BIT_HUGE=128
};
static const uint64_t FRAME_MASK=UINT64_C(0x000ffffffffff000);

/* Read little-endian u64 from a caller-validated table page. */
static uint64_t entry64(const uint8_t *bytes)
{
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) value |= (uint64_t)bytes[i]<<(i*8);
    return value;
}

/* Require a 48-bit or 57-bit canonical address matching paging depth;
 * the upper bits must equal sign extension of bit width-1. */
static bool canonical(uint64_t address, uint8_t levels)
{
    unsigned bits=levels==4?48u:57u;
    uint64_t low=(UINT64_C(1) << (bits-1))-1;
    return address<=low || address>=UINT64_MAX-low;
}

/* Walk from CR3 to leaf, honoring huge pages at PDPT (1GiB) and PDE (2MiB).
 * Any user-enabled ancestor makes the mapping unsafe for our MMIO preflight,
 * even if the leaf is supervisor-only. All table reads use mapper ownership. */
bool x86_page_walk_read_only(const struct x86_page_walker *w,
                              uint64_t address,
                              struct x86_page_translation *out)
{
    if (w==0||out==0||w->map_table==0||
        (w->paging_levels!=4&&w->paging_levels!=5)||
        !canonical(address,w->paging_levels)||
        (w->cr3_physical&~FRAME_MASK)!=0 ||
        (w->cr3_physical&FRAME_MASK)==0)
        return false;
    uint64_t table=w->cr3_physical & FRAME_MASK;
    bool writable=true;
    for (unsigned level=w->paging_levels;level>=1;--level) {
        const uint8_t *bytes=w->map_table(w->context,table);
        if(bytes==0) return false;
        unsigned shift=12u+9u*(level-1);
        uint64_t index=(address>>shift)&511u;
        uint64_t entry=entry64(bytes+index*ENTRY_BYTES);
        if((entry&BIT_PRESENT)==0 || (entry&BIT_USER)!=0) return false;
        if((entry&BIT_WRITABLE)==0) writable=false;
        bool huge=(entry&BIT_HUGE)!=0;
        if(huge && level!=3 && level!=2) return false;
        if(level==1 || huge) {
            uint64_t page_size=UINT64_C(1)<<shift;
            uint64_t mask=page_size-1;
            uint64_t base=entry&FRAME_MASK;
            if((base&mask)!=0) return false;
            if(base>UINT64_MAX-(address&mask)) return false;
            struct x86_page_translation result={
                .physical=base+(address&mask),
                .page_size=page_size,
                .leaf_entry=entry,
                .supervisor_only=true,
                .writable=writable
            };
            *out=result;
            return true;
        }
        table=entry&FRAME_MASK;
        if(table==0 || (table&(TABLE_BYTES-1))!=0) return false;
    }
    return false;
}
