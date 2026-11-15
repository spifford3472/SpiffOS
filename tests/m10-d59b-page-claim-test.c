/* Purpose: exercise callback-free M3 claim publication and exact rollback.
 * Dependencies: actual M1/M2/M3 plus link-only release failure injection.
 * Invariants: numeric page values are never dereferenced; native test only.
 * Reference: ADR-m10-d59b-runtime-memory-wiring.md, AGENTS.md. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "page-access.h"

static bool fail_release;
enum frame_result __real_frame_release(struct frame_allocator *,
    enum frame_owner,uint64_t);
/* Native linker fault seam; never changes production source. Preserve the
 * allocated frame when instructed, modeling uncertain rollback ownership. */
enum frame_result __wrap_frame_release(struct frame_allocator *allocator,
    enum frame_owner owner,uint64_t physical)
{
    if(fail_release) return FRAME_INVALID;
    return __real_frame_release(allocator,owner,physical);
}
struct fixture {
    struct memory_region region;
    union { uint64_t align; unsigned char bytes[64]; } storage;
    struct frame_allocator allocator;
    struct page_access_policy policy;
};
/* Initialize eight physical frames, one reserved, with aligned host metadata.
 * Descriptor and borrowed slots stay live until the caller's test returns. */
static void setup(struct fixture *f)
{
    memset(f,0,sizeof *f);
    f->region=(struct memory_region){UINT64_C(0x100000),8u*MEMORY_PAGE_BYTES,MEMORY_REGION_USABLE};
    assert(frame_allocator_initialize(&f->allocator,&f->region,1,
        f->storage.bytes+8u,32u,1u)==FRAME_OK);
    f->policy=(struct page_access_policy){&f->allocator,UINT64_C(0xffff800000000000),48u};
}
/* Reject one request with all allocator/slot/output bytes unchanged. */
static void unchanged(struct fixture *f,enum page_access_result expected)
{
    struct frame_allocator allocator=f->allocator;
    unsigned char slots[64];
    memcpy(slots,f->storage.bytes,sizeof slots);
    struct page_access_claim out={UINT64_C(0x12340),UINT64_C(0x56780)};
    assert(page_access_claim(&f->allocator,&f->policy,FRAME_OWNER_KERNEL,&out)==expected);
    assert(out.physical_address==UINT64_C(0x12340) && out.mapped_address==UINT64_C(0x56780));
    assert(memcmp(&allocator,&f->allocator,sizeof allocator)==0);
    assert(memcmp(slots,f->storage.bytes,sizeof slots)==0);
}
/* Main runs real allocation/authorization/rollback and never touches fake RAM.
 * Every successful claim is explicitly released by its exact owner. */
int main(void)
{
    struct fixture f;
    setup(&f);
    const uint64_t ordinary=f.allocator.ordinary_free;
    for(unsigned i=0;i<20000u;++i) {
        enum frame_owner owner=(i&1u)?FRAME_OWNER_KERNEL:FRAME_OWNER_SESSION;
        struct page_access_claim out={0,0};
        assert(page_access_claim(&f.allocator,&f.policy,owner,&out)==PAGE_ACCESS_OK);
        assert(out.physical_address==UINT64_C(0x101000));
        assert(out.mapped_address==UINT64_C(0xffff800000101000));
        assert(f.allocator.ordinary_free==ordinary-1u && f.allocator.reserve_free==1u);
        uint64_t resolved=0;
        assert(page_access_resolve(&f.policy,owner,out.physical_address,&resolved)==PAGE_ACCESS_OK);
        assert(resolved==out.mapped_address);
        enum frame_owner other=(owner==FRAME_OWNER_KERNEL)?FRAME_OWNER_SESSION:FRAME_OWNER_KERNEL;
        assert(frame_release(&f.allocator,other,out.physical_address)==FRAME_WRONG_OWNER);
        assert(frame_release(&f.allocator,owner,out.physical_address)==FRAME_OK);
        assert(frame_allocator_check(&f.allocator) && f.allocator.ordinary_free==ordinary);
    }
    f.policy.virtual_address_bits=47u;
    unchanged(&f,PAGE_ACCESS_UNSUPPORTED);
    f.policy.virtual_address_bits=48u;
    f.policy.direct_map_offset=UINT64_C(0x800000000000);
    unchanged(&f,PAGE_ACCESS_NONCANONICAL);
    f.policy.direct_map_offset=UINT64_MAX-UINT64_C(0x100000);
    unchanged(&f,PAGE_ACCESS_ADDRESS_OVERFLOW);
    f.policy.direct_map_offset=0;
    struct page_access_claim out={11u,22u};
    assert(page_access_claim(NULL,&f.policy,FRAME_OWNER_KERNEL,&out)==PAGE_ACCESS_INVALID);
    assert(page_access_claim(&f.allocator,NULL,FRAME_OWNER_KERNEL,&out)==PAGE_ACCESS_INVALID);
    assert(page_access_claim(&f.allocator,&f.policy,FRAME_OWNER_KERNEL,NULL)==PAGE_ACCESS_INVALID);
    assert(page_access_claim(&f.allocator,&f.policy,FRAME_OWNER_FREE,&out)==PAGE_ACCESS_INVALID);
    struct frame_allocator other={0};
    f.policy.allocator=&other;
    assert(page_access_claim(&f.allocator,&f.policy,FRAME_OWNER_KERNEL,&out)==PAGE_ACCESS_INVALID);
    f.policy.allocator=&f.allocator;
    assert(out.physical_address==11u && out.mapped_address==22u);
    struct fixture before=f;
    assert(page_access_claim(&f.allocator,&f.policy,FRAME_OWNER_KERNEL,
        (struct page_access_claim *)(void *)&f.allocator)==PAGE_ACCESS_INVALID);
    /* Only the SECOND output aliases used ownership slots. The first lies in
     * a valid aligned prefix object, so both field checks are necessary. */
    assert(page_access_claim(&f.allocator,&f.policy,FRAME_OWNER_KERNEL,
        (struct page_access_claim *)(void *)f.storage.bytes)==PAGE_ACCESS_INVALID);
    assert(memcmp(&before,&f,sizeof f)==0);
    ++f.allocator.ordinary_free;
    unchanged(&f,PAGE_ACCESS_INVALID);
    --f.allocator.ordinary_free;
    uint64_t pages[7];
    for(unsigned i=0;i<7u;++i)
        assert(frame_allocate(&f.allocator,FRAME_OWNER_SESSION,false,&pages[i])==FRAME_OK);
    unchanged(&f,PAGE_ACCESS_ALLOCATION_FAILED);
    for(unsigned i=0;i<7u;++i)
        assert(frame_release(&f.allocator,FRAME_OWNER_SESSION,pages[i])==FRAME_OK);
    f.policy.virtual_address_bits=1u;
    fail_release=true;
    assert(page_access_claim(&f.allocator,&f.policy,FRAME_OWNER_KERNEL,&out)==PAGE_ACCESS_ROLLBACK_FAILED);
    assert(out.physical_address==11u && out.mapped_address==22u);
    assert(f.allocator.ordinary_free==ordinary-1u && f.allocator.owned[FRAME_OWNER_KERNEL]==1u);
    fail_release=false;
    assert(frame_release(&f.allocator,FRAME_OWNER_KERNEL,UINT64_C(0x101000))==FRAME_OK);
    assert(frame_allocator_check(&f.allocator));
    puts("SPIFFOS_M10_D59B_PAGE_CLAIM_HOSTED_OK");
    return 0;
}
