/* Purpose: exercise actual admitted M2/M3/M4/M6 and boot API boundaries.
 * Dependencies: real memory-boot translation unit included ONCE for native
 * white-box setup of its otherwise private boot RAM/stack ownership records.
 * Invariants: only CPU samples and linker release fault are simulated; no
 * replacement allocator/registry, fake callback authority or hardware claim.
 * References: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md. */
#include "m10-d59b-test-cpu.h"
#include <stdio.h>
#include "../src/kernel/memory-boot.c"

static _Alignas(16) uint8_t arena[MEMORY_SERVICE_MIN_ARENA_BYTES];
static struct memory_region native_region;
static uint8_t native_slots[32];
static uint64_t injected_failure;
static unsigned release_calls,writer_calls,backend_calls;
enum frame_result __real_frame_release(struct frame_allocator *,
    enum frame_owner,uint64_t);
/* Link-only exact release failure; keep ownership unchanged on injected error.
 * No such injection branch exists in production allocator source. */
enum frame_result __wrap_frame_release(struct frame_allocator *allocator,
    enum frame_owner owner,uint64_t physical)
{
    ++release_calls;
    if(physical==injected_failure) return FRAME_INVALID;
    return __real_frame_release(allocator,owner,physical);
}
/* Refused boot reports must never reach this native observer. */
static bool writer(const char *text)
{ (void)text;++writer_calls;return true; }
/* Refused generic runtime page callbacks must never reach this observer.
 * Numeric mapped addresses are not host pointers and are never dereferenced. */
static bool backend(void *opaque,uint64_t mapped,size_t bytes)
{ (void)opaque;(void)mapped;(void)bytes;++backend_calls;return false; }
/* Use the ACTUAL memory-boot stack releaser from the D5.2b owner callback. */
static bool retire_owner(size_t owner_index)
{ return memory_boot_m10_task_stack_release(owner_index); }

/* Bootstrap a true small boot allocator/service, retain exact stack pages,
 * create the reserved supervisor and bind from the surviving fake CPU stack.
 * Every allocator call uses the same compiled admission wrappers as the guest. */
static struct kernel_task_handle prepare(struct fixture *f)
{
    setup(f);
    f->cpu.stack=UINT64_C(0x200000);
    assert(d59b_memory_host_backend(&f->backend));
    assert(d59b_memory_bootstrap());
    native_region=(struct memory_region){UINT64_C(0x100000),
        32u*MEMORY_PAGE_BYTES,MEMORY_REGION_USABLE};
    assert(frame_allocator_initialize(&boot_allocator,&native_region,1,
        native_slots,sizeof native_slots,0)==FRAME_OK);
    boot_allocator_available=true;
    retained_policy=(struct page_access_policy){&boot_allocator,0,48u};
    assert(memory_service_initialize(&retained_service,arena,sizeof arena)==MEMORY_SERVICE_OK);
    retained_started=true;
    for(unsigned i=0;i<20u;++i) {
        uint64_t physical=0;
        assert(frame_allocate(&boot_allocator,FRAME_OWNER_KERNEL,false,&physical)==FRAME_OK);
        assert(physical==UINT64_C(0x100000)+i*MEMORY_PAGE_BYTES);
    }
    for(unsigned i=0;i<4u;++i) {
        m10_b_stack_physical[0][i]=UINT64_C(0x100000)+i*MEMORY_PAGE_BYTES;
        m10_b_stack_physical[1][i]=UINT64_C(0x110000)+i*MEMORY_PAGE_BYTES;
    }
    m10_b_stack_active[0]=m10_b_stack_active[1]=true;
    assert(kernel_task_terminate(&f->tasks,f->current)==KERNEL_TASK_OK);
    struct kernel_task_handle supervisor={0};
    assert(kernel_task_create(&f->tasks,true,&supervisor)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding binding={UINT64_C(0x110000),16384u,
        UINT64_C(0x113fe0),1u};
    assert(kernel_task_resources_bind(&f->resources,&f->tasks,supervisor,&binding)==KERNEL_RESOURCE_OK);
    assert(kernel_task_make_runnable(&f->tasks,supervisor)==KERNEL_TASK_OK);
    struct kernel_task_handle selected={0};
    assert(kernel_task_dispatch(&f->tasks,&selected)==KERNEL_TASK_OK);
    assert(d59b_memory_bind(&f->context));
    f->cpu.stack=UINT64_C(0x112000);
    return supervisor;
}
/* Ensure all sixteen unsupported boot entry points refuse before callback,
 * output publication, snapshot work, root changes or stack release. */
static void boot_refusals(void)
{
    uint64_t physical=UINT64_C(0x12340);
    uint64_t *mapped=(void *)(uintptr_t)UINT64_C(0x56780);
    uint32_t *pages[1]={(void *)(uintptr_t)UINT64_C(0x67890)};
    void *stack=(void *)(uintptr_t)UINT64_C(0x789a0);
    struct page_access_policy policy={&boot_allocator,99u,48u},before=policy;
    assert(!memory_boot_report(NULL,writer));
    assert(!memory_boot_allocator_probe(writer));
    assert(!memory_boot_page_policy(NULL,NULL,&boot_allocator,&policy));
    assert(memcmp(&before,&policy,sizeof policy)==0);
    assert(!memory_boot_page_probe(NULL,NULL,writer));
    assert(!memory_boot_heap_probe(NULL,NULL,writer));
    assert(!memory_boot_service_start(NULL,NULL,writer));
    assert(!memory_boot_service_check(writer));
    assert(!memory_boot_service_editor_check(writer));
    assert(!memory_boot_select_workspace(1u));
    assert(!memory_boot_reset_workspace(1u));
    assert(!memory_boot_mmio_table_acquire(&physical,&mapped));
    assert(!memory_boot_mmio_table_release(UINT64_C(0x100000)));
    assert(!memory_boot_snapshot_prepare(1u,1u,pages));
    assert(!memory_boot_m10_task_stack_prepare(0,&stack));
    assert(!memory_boot_m10_task_stack_release(0));
    assert(!memory_boot_m10_task_stack_failure_probe());
    uint64_t scope_frames=UINT64_C(0xabcdef);
    assert(!memory_boot_d59b_scope_policy(&physical,&scope_frames));
    assert(scope_frames==UINT64_C(0xabcdef));
    assert(writer_calls==0 && physical==UINT64_C(0x12340));
    assert(mapped==(void *)(uintptr_t)UINT64_C(0x56780));
    assert(pages[0]==(void *)(uintptr_t)UINT64_C(0x67890));
    assert(stack==(void *)(uintptr_t)UINT64_C(0x789a0));
}
/* Fresh process per sticky failure; no production reset function is exposed. */
int main(int argc,char **argv)
{
    assert(argc==2);
    struct fixture f;
    const struct kernel_task_handle supervisor=prepare(&f);
    f.cpu.enabled=true;
    if(strcmp(argv[1],"irq")==0 || strcmp(argv[1],"cpu")==0 ||
       strcmp(argv[1],"rsp")==0) {
        if(strcmp(argv[1],"irq")==0) d59b_irq_depth=1;
        if(strcmp(argv[1],"cpu")==0) ++f.cpu.id;
        if(strcmp(argv[1],"rsp")==0) f.cpu.stack=UINT64_C(0x200000);
        struct frame_allocator before=boot_allocator;
        uint8_t slots[sizeof native_slots];memcpy(slots,native_slots,sizeof slots);
        uint64_t output=UINT64_C(0x12340);
        assert(frame_allocate(&boot_allocator,FRAME_OWNER_KERNEL,false,&output)==FRAME_INVALID);
        assert(output==UINT64_C(0x12340) && memcmp(&before,&boot_allocator,sizeof before)==0);
        assert(memcmp(slots,native_slots,sizeof slots)==0 && !f.cpu.enabled && f.guard.faulted);
        puts("MEMORY_DIRECT_EXECUTION_DENIED");return 0;
    }
    if(strcmp(argv[1],"void-retire")==0) {
        memory_service_retire(&retained_service);
        assert(!"runtime void lifecycle returned");return 1;
    }
    assert(strcmp(argv[1],"happy")==0 || strcmp(argv[1],"partial-release")==0);
    struct memory_service before_service=retained_service;
    uint8_t before_arena[sizeof arena];memcpy(before_arena,arena,sizeof arena);
    boot_refusals();
    assert(memory_service_reset_workspace(&retained_service,1u)==MEMORY_SERVICE_INVALID);
    assert(kernel_heap_release_workspace(&retained_service.heap,KERNEL_HEAP_OWNER_FIRST_WORKSPACE)==KERNEL_HEAP_INVALID);
    assert(kernel_heap_release_session(&retained_service.heap)==KERNEL_HEAP_INVALID);
    struct frame_allocator fresh_allocator={0},saved=fresh_allocator;
    uint8_t fresh_slots[32];memset(fresh_slots,0xa5,sizeof fresh_slots);
    assert(frame_allocator_initialize(&fresh_allocator,&native_region,1,
        fresh_slots,sizeof fresh_slots,0)==FRAME_INVALID);
    assert(memcmp(&fresh_allocator,&saved,sizeof saved)==0);
    for(size_t i=0;i<sizeof fresh_slots;++i) assert(fresh_slots[i]==0xa5);
    assert(memcmp(&before_service,&retained_service,sizeof before_service)==0);
    assert(memcmp(before_arena,arena,sizeof arena)==0);
    assert(f.cpu.enabled && !f.guard.faulted && f.guard.depth==0u);
    /* Refuse an untested tracking size BEFORE attempting any oversized scan. */
    struct frame_allocator oversized=boot_allocator;
    oversized.total_frames=UINT64_C(131073);
    const struct frame_allocator saved_oversized=oversized;
    uint64_t cap_output=UINT64_C(0x12340);
    assert(frame_allocate(&oversized,FRAME_OWNER_KERNEL,false,&cap_output)==FRAME_INVALID);
    assert(!frame_allocator_check(&oversized));
    assert(!frame_allocator_address(&oversized,0,&cap_output));
    assert(frame_release(&oversized,FRAME_OWNER_KERNEL,UINT64_C(0x100000))==FRAME_INVALID);
    assert(cap_output==UINT64_C(0x12340));
    assert(memcmp(&oversized,&saved_oversized,sizeof oversized)==0);
    /* Original output alias protection must not be bypassed by staging in a
     * wrapper. These disallowed output objects are rejected before any write. */
    assert(memory_service_get_session(&retained_service,
        (const struct memory_service_session **)(void *)&retained_service.session_root)==MEMORY_SERVICE_INVALID);
    assert(memory_service_get_workspace(&retained_service,1u,
        (const struct memory_service_workspace **)(void *)&retained_service.workspace_roots[0])==MEMORY_SERVICE_INVALID);
    assert(memory_service_allocate(&retained_service,KERNEL_HEAP_OWNER_KERNEL,16u,
        (void **)(void *)&retained_service.session_root)==MEMORY_SERVICE_INVALID);
    assert(kernel_heap_allocate(&retained_service.heap,KERNEL_HEAP_OWNER_KERNEL,16u,
        (void **)(void *)&retained_service.heap.arena)==KERNEL_HEAP_INVALID);
    assert(memcmp(&before_service,&retained_service,sizeof before_service)==0);
    assert(memcmp(before_arena,arena,sizeof arena)==0);
    uint64_t refused=UINT64_C(0x12340);
    assert(page_access_zero(&retained_policy,FRAME_OWNER_KERNEL,UINT64_C(0x100000),
        backend,NULL)==PAGE_ACCESS_INVALID);
    assert(page_access_allocate_zero(&boot_allocator,&retained_policy,FRAME_OWNER_KERNEL,
        backend,NULL,&refused)==PAGE_ACCESS_INVALID);
    assert(refused==UINT64_C(0x12340) && backend_calls==0);
    struct page_access_claim private={UINT64_C(0x12340),UINT64_C(0x56780)};
    assert(page_access_claim(&boot_allocator,&retained_policy,FRAME_OWNER_KERNEL,&private)==PAGE_ACCESS_INVALID);
    assert(private.physical_address==UINT64_C(0x12340) && private.mapped_address==UINT64_C(0x56780));
    uint32_t cookie=0;
    assert(d59b_enter(&f.context,&cookie));
    assert(page_access_claim(&boot_allocator,&retained_policy,FRAME_OWNER_KERNEL,&private)==PAGE_ACCESS_OK);
    assert(frame_release(&boot_allocator,FRAME_OWNER_KERNEL,private.physical_address)==FRAME_OK);
    assert(d59b_exit(&f.context,cookie) && f.cpu.enabled);
    const uint64_t free_before=boot_allocator.ordinary_free;
    for(unsigned i=0;i<20000u;++i) {
        f.cpu.enabled=(i&1u)!=0;
        const bool enabled=f.cpu.enabled;
        uint64_t physical=0;
        assert(frame_allocate(&boot_allocator,FRAME_OWNER_KERNEL,false,&physical)==FRAME_OK);
        assert(physical==UINT64_C(0x114000) && f.cpu.enabled==enabled);
        assert(frame_release(&boot_allocator,FRAME_OWNER_KERNEL,UINT64_C(0x110000))==FRAME_FORBIDDEN);
        assert(frame_release(&boot_allocator,FRAME_OWNER_KERNEL,UINT64_C(0x100000))==FRAME_FORBIDDEN);
        assert(frame_release(&boot_allocator,FRAME_OWNER_SESSION,physical)==FRAME_WRONG_OWNER);
        assert(frame_release(&boot_allocator,FRAME_OWNER_KERNEL,physical)==FRAME_OK);
        void *allocation=(void *)(uintptr_t)UINT64_C(0x12340);
        assert(memory_service_allocate(&retained_service,KERNEL_HEAP_OWNER_FIRST_WORKSPACE,
            32u,&allocation)==MEMORY_SERVICE_OK);
        uint8_t *bytes=allocation;
        for(size_t j=0;j<32u;++j) {assert(bytes[j]==0);bytes[j]=(uint8_t)(j+1u);}
        assert(memory_service_release(&retained_service,KERNEL_HEAP_OWNER_FIRST_WORKSPACE,
            allocation)==MEMORY_SERVICE_OK);
        assert(boot_allocator.ordinary_free==free_before && f.cpu.enabled==enabled);
        assert(f.guard.depth==0u && !f.guard.faulted);
    }
    f.cpu.enabled=false;
    assert(kernel_task_terminate(&f.tasks,supervisor)==KERNEL_TASK_OK);
    f.cpu.stack=UINT64_C(0x200000);
    assert(d59b_memory_unbind(&f.context));
    if(strcmp(argv[1],"partial-release")==0) {
        injected_failure=UINT64_C(0x102000);
        assert(kernel_task_resources_retire(&f.resources,&f.tasks,f.current,
            f.cpu.stack,retire_owner)==KERNEL_RESOURCE_QUARANTINED);
        assert(m10_b_stack_physical[0][3]==0);
        assert(m10_b_stack_physical[0][2]==UINT64_C(0x102000));
        assert(m10_b_stack_physical[0][1]==UINT64_C(0x101000));
        assert(m10_b_stack_physical[0][0]==UINT64_C(0x100000));
        assert(m10_b_stack_poisoned && m10_b_stack_active[0]);
        const unsigned calls=release_calls;
        assert(!memory_boot_m10_task_stack_release(0) && release_calls==calls);
        uint64_t next=0;
        assert(frame_allocate(&boot_allocator,FRAME_OWNER_KERNEL,false,&next)==FRAME_OK);
        assert(next==UINT64_C(0x114000)); /* 0x103000 FREE but quarantined. */
        assert(f.resources.task[f.current.slot].quarantined);
        assert(!f.tasks.tasks[f.current.slot].reclaimed);
        assert(frame_allocator_check(&boot_allocator));
        puts("MEMORY_PARTIAL_RELEASE_TRACKING_QUARANTINE_OK");return 0;
    }
    assert(kernel_task_resources_retire(&f.resources,&f.tasks,f.current,
        f.cpu.stack,retire_owner)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_retire(&f.resources,&f.tasks,supervisor,
        f.cpu.stack,retire_owner)==KERNEL_RESOURCE_OK);
    assert(boot_allocator.ordinary_free==free_before+8u);
    assert(d59b_memory_retirement_complete(&f.context));
    puts("SPIFFOS_M10_D59B_MEMORY_DIRECT_HOSTED_OK");
    return 0;
}
