/* Purpose: hosted transactional MMIO mapping/rollback/fault-injection tests.
 * Dependencies: pure x86-mmio-map.c, mmio-page-policy.c, injected owner frames.
 * Invariants: no real device MMIO, CR3 writes, interrupts, privileged I/O.
 * Reference: docs/decisions/ADR-m9-3d4-mmio-mapping.md. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "x86-mmio-map.h"
#include "x86-page-walk.h"

enum { TEST_PAGES=5, PAGE_WORDS=512 };
static _Alignas(4096) uint64_t frames[TEST_PAGES][PAGE_WORDS];
static _Alignas(4096) uint64_t root[PAGE_WORDS];
struct simulated {
    unsigned allocations;
    unsigned live;
    unsigned releases;
    unsigned invalidate;
    unsigned fail_at;
    bool wrong_frame;
    bool stale_frame;
    bool reject_release;
};

/* Allocate/zero one deterministic simulated physical RAM frame, subject to
 * an injected failure at every acquisition point. Counters track ownership. */
static bool acquire(void *context,uint64_t *physical,uint64_t **writable)
{
    struct simulated *s=context;
    if(s->fail_at==s->allocations+1) return false;
    if(s->allocations>=TEST_PAGES) return false;
    unsigned index=s->allocations++;
    s->live++;
    *physical=(uint64_t)(index+2)*4096u;
    *writable=frames[index];
    memset(frames[index],0,sizeof frames[index]);
    if(s->wrong_frame) *physical=0;
    if(s->stale_frame) frames[index][0]=1;
    return true;
}

/* Return one exactly owned frame; reject injected rollback failures. */
static bool release(void *context,uint64_t physical)
{
    struct simulated *s=context;
    if(s->reject_release) return false;
    assert(physical%4096==0);
    ++s->releases;--s->live;
    return true;
}

/* Count TLB invalidation callbacks; hosted tests never execute INVLPG. */
static void invalidate(void *context,uint64_t virtual_address)
{
    struct simulated *s=context;
    assert(virtual_address==MMIO_APIC_LAPIC_VIRTUAL ||
           virtual_address==MMIO_APIC_IOAPIC_VIRTUAL);
    ++s->invalidate;
}

/* Translate only owned synthetic root and staging pages for walker proof. */
static const uint8_t *map_table(void *unused,uint64_t phys)
{
    (void)unused;
    if(phys==4096) return (const uint8_t *)root;
    for(unsigned i=0;i<TEST_PAGES;i++)
        if(phys==(uint64_t)(i+2)*4096u)
            return (const uint8_t *)frames[i];
    return NULL;
}

/* One complete mapping must be supervisor, writable, NX, UC-index-3 and
 * resolve both exact physical controller addresses, then roll back cleanly. */
static void valid_test(uint8_t levels)
{
    memset(root,0,sizeof root);memset(frames,0,sizeof frames);
    struct simulated s={0};
    struct x86_mmio_map_backend backend={acquire,release,invalidate,&s};
    struct mmio_page_cpu_policy cpu={
        .ia32_pat_msr=UINT64_C(0x0007040600070406),
        .nx_supported=true,.nxe_enabled=true,.physical_address_bits=48
    };
    struct x86_mmio_mapping state={0};
    assert(x86_mmio_map_create(&state,levels,root,0xfee00000,0xfec00000,
                               &cpu,&backend));
    assert(state.active && state.table_count==(size_t)levels-1);
    assert(s.live==(unsigned)levels-1 && s.invalidate==2);
    struct x86_page_walker walk={map_table,NULL,4096,levels};
    struct x86_page_translation t={0};
    assert(x86_page_walk_read_only(&walk,MMIO_APIC_LAPIC_VIRTUAL,&t));
    assert(t.physical==0xfee00000 && t.writable && t.supervisor_only);
    assert((t.leaf_entry & (UINT64_C(1)<<63))!=0);
    assert((t.leaf_entry & UINT64_C(24))==UINT64_C(24));
    assert(x86_page_walk_read_only(&walk,MMIO_APIC_IOAPIC_VIRTUAL,&t));
    assert(t.physical==0xfec00000 && t.page_size==4096);
    assert(!x86_mmio_map_create(&state,levels,root,0xfee00000,0xfec00000,
                                &cpu,&backend));
    assert(x86_mmio_map_destroy(&state,&backend));
    assert(s.live==0 && s.invalidate==4);
    for(unsigned j=0;j<PAGE_WORDS;++j) assert(root[j]==0);
    assert(!x86_page_walk_read_only(&walk,MMIO_APIC_LAPIC_VIRTUAL,&t));
    assert(!x86_mmio_map_destroy(&state,&backend));
}

/* Fail each staged allocation. Every successfully acquired page is returned
 * and the root remains entirely untouched, including adjacent slots. */
static void allocation_fail_tests(uint8_t levels)
{
    for(unsigned failure=1;failure<levels;++failure) {
        memset(root,0,sizeof root);memset(frames,0,sizeof frames);
        struct simulated s={.fail_at=failure};
        struct x86_mmio_map_backend backend={acquire,release,invalidate,&s};
        struct mmio_page_cpu_policy cpu={
            .ia32_pat_msr=UINT64_C(0x0007040600070406),
            .nx_supported=true,.nxe_enabled=true,.physical_address_bits=48
        };
        struct x86_mmio_mapping state={0};
        assert(!x86_mmio_map_create(&state,levels,root,0xfee00000,
                                    0xfec00000,&cpu,&backend));
        assert(s.live==0 && !state.active && !state.poisoned);
        for(unsigned j=0;j<PAGE_WORDS;++j) assert(root[j]==0);
    }
}

/* Refuse occupied root, duplicate/invalid physical addresses and a stale
 * or unowned acquisition, without claiming a published mapping. */
static void rejection_tests(void)
{
    memset(root,0,sizeof root);memset(frames,0,sizeof frames);
    struct simulated s={0};
    struct x86_mmio_map_backend backend={acquire,release,invalidate,&s};
    struct mmio_page_cpu_policy cpu={
        .ia32_pat_msr=UINT64_C(0x0007040600070406),
        .nx_supported=true,.nxe_enabled=true,.physical_address_bits=48
    };
    struct x86_mmio_mapping state={0};
    size_t index=(size_t)((MMIO_APIC_LAPIC_VIRTUAL>>39)&511);
    root[index]=UINT64_C(0x5003);
    assert(!x86_mmio_map_create(&state,4,root,0xfee00000,0xfec00000,
                                &cpu,&backend));
    assert(root[index]==UINT64_C(0x5003) && s.allocations==0);
    root[index]=0;
    assert(!x86_mmio_map_create(&state,4,root,0xfee00000,0xfee00000,
                                &cpu,&backend));
    cpu.nxe_enabled=false;
    assert(!x86_mmio_map_create(&state,4,root,0xfee00000,0xfec00000,
                                &cpu,&backend));
    cpu.nxe_enabled=true;
    s.stale_frame=true;
    assert(!x86_mmio_map_create(&state,4,root,0xfee00000,0xfec00000,
                                &cpu,&backend));
    assert(s.live==0 && !state.active);
    s.stale_frame=false;
    s.wrong_frame=true;
    assert(!x86_mmio_map_create(&state,4,root,0xfee00000,0xfec00000,
                                &cpu,&backend));
    assert(s.live==0 && !state.active);
}

/* If the allocator refuses a rollback release, the mapping service must
 * poison its state and forbid retries. The root stays unpublished, so a
 * corrupt owner ledger cannot become an active page-table reference. */
static void rollback_poison_test(void)
{
    memset(root,0,sizeof root);memset(frames,0,sizeof frames);
    struct simulated s={.fail_at=2,.reject_release=true};
    struct x86_mmio_map_backend backend={acquire,release,invalidate,&s};
    struct mmio_page_cpu_policy cpu={
        .ia32_pat_msr=UINT64_C(0x0007040600070406),
        .nx_supported=true,.nxe_enabled=true,.physical_address_bits=48
    };
    struct x86_mmio_mapping state={0};
    assert(!x86_mmio_map_create(&state,4,root,0xfee00000,0xfec00000,
                                &cpu,&backend));
    assert(state.poisoned && !state.active);
    assert(s.live==1);
    for(unsigned i=0;i<PAGE_WORDS;++i) assert(root[i]==0);
    s.reject_release=false;s.fail_at=0;
    assert(!x86_mmio_map_create(&state,4,root,0xfee00000,0xfec00000,
                                &cpu,&backend));
}

/* Validate both page depths, rollback and negative paths. */
int main(void)
{
    valid_test(4);valid_test(5);
    allocation_fail_tests(4);allocation_fail_tests(5);
    rejection_tests();rollback_poison_test();
    return 0;
}
