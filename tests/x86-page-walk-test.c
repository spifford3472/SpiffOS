/* Purpose: pure hosted x86 page-table walker regression/negative tests.
 * No privileged CR3/MSR, hardware, MMIO or page-table modifications. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "x86-page-walk.h"

static uint8_t frames[5][4096];
static const uint64_t addresses[5]={0x1000,0x2000,0x3000,0x4000,0x5000};

/* Allow only one of five explicitly owned test frames. */
static const uint8_t *map(void *unused,uint64_t physical)
{
    (void)unused;
    for(unsigned i=0;i<5;++i) if(addresses[i]==physical) return frames[i];
    return NULL;
}

/* Store a little-endian 64-bit PTE into an assigned page-table slot. */
static void put(unsigned page,unsigned index,uint64_t value)
{
    assert(page<5 && index<512);
    uint8_t *p=frames[page]+index*8;
    for(unsigned i=0;i<8;++i) p[i]=(uint8_t)(value>>(i*8));
}

/* Verify present mapping, 2MiB/1GiB leaves, invalid ranges and no writes. */
int main(void)
{
    memset(frames,0,sizeof frames);
    struct x86_page_walker w={map,NULL,0x1000,4};
    struct x86_page_translation t={0};
    uint64_t virt=UINT64_C(0xffff800000001234);
    unsigned pml4=(unsigned)((virt>>39)&511u);
    unsigned pdpt=(unsigned)((virt>>30)&511u);
    unsigned pd=(unsigned)((virt>>21)&511u);
    unsigned pt=(unsigned)((virt>>12)&511u);
    assert(!x86_page_walk_read_only(&w,virt,&t));
    assert(x86_page_probe_read_only(&w,virt)==X86_PAGE_PROBE_ABSENT);
    put(0,pml4,0x2003);
    put(1,pdpt,0x3003);
    put(2,pd,0x4003);
    put(3,pt,0x7003);
    assert(x86_page_walk_read_only(&w,virt,&t));
    assert(t.physical==0x7234u && t.page_size==4096);
    assert(x86_page_probe_read_only(&w,virt)==X86_PAGE_PROBE_PRESENT);
    /* A present mapping through a user-accessible ancestor remains a real
     * alias even though supervisor-only page authorization rejects it. */
    put(0,pml4,0x2007);
    assert(x86_page_probe_read_only(&w,virt)==X86_PAGE_PROBE_PRESENT);
    assert(!x86_page_walk_read_only(&w,virt,&t));
    put(0,pml4,0x2003);
    assert(t.supervisor_only && t.writable);
    assert(x86_page_probe_read_only(NULL,virt)==X86_PAGE_PROBE_UNKNOWN);
    uint64_t saved_cr3=w.cr3_physical;
    w.cr3_physical=0x9000;
    assert(x86_page_probe_read_only(&w,virt)==X86_PAGE_PROBE_UNKNOWN);
    w.cr3_physical=saved_cr3;
    put(3,pt,0x7007); /* leaf user bit invalidates MMIO use */
    assert(!x86_page_walk_read_only(&w,virt,&t));
    put(3,pt,0x7003);
    put(2,pd,0x200083); /* 2MiB leaf */
    assert(x86_page_walk_read_only(&w,virt,&t));
    assert(t.page_size==UINT64_C(2)*1024*1024);
    put(1,pdpt,0x40000083); /* 1GiB leaf */
    assert(x86_page_walk_read_only(&w,virt,&t));
    assert(t.page_size==UINT64_C(1)*1024*1024*1024);
    put(1,pdpt,0x40000003);
    assert(!x86_page_walk_read_only(&w,virt,&t));
    put(1,pdpt,0x3003);
    put(2,pd,0x4003);
    assert(!x86_page_walk_read_only(&w,UINT64_C(0x0000800000000000),&t));
    assert(!x86_page_walk_read_only(NULL,virt,&t));
    assert(!x86_page_walk_read_only(&w,virt,NULL));
    w.paging_levels=3;
    assert(!x86_page_walk_read_only(&w,virt,&t));
    w.paging_levels=4;
    w.cr3_physical=0;
    assert(!x86_page_walk_read_only(&w,virt,&t));
    w.cr3_physical=0x1000;
    put(1,pdpt,0x3007);
    assert(!x86_page_walk_read_only(&w,virt,&t));
    return 0;
}
