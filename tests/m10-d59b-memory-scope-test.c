/* Purpose: verify the real memory-scope lifecycle with native CPU samples.
 * Dependencies: actual D5.7, M10-A and D5.2b; no fake scheduler or allocator.
 * Invariants: each invocation owns one fresh domain; no production reset API.
 * Reference: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md. */
#include "m10-d59b-test-cpu.h"
#include <stdio.h>

/* Test-only direct policy provider, replacing the Limine boot bridge at link.
 * Numeric HHDM=0 is never dereferenced. No callback registration in kernel. */
bool memory_boot_d59b_scope_policy(uint64_t *offset,uint64_t *frames)
{
    if(offset==NULL || frames==NULL) return false;
    *offset=0;*frames=64u;return true;
}
static unsigned released;
/* Native resource owner seam counts safe retirement, never frees host RAM. */
static bool release_owner(size_t owner_index)
{
    assert(owner_index<2u);++released;return true;
}
/* Register SUP against the same task table/ledger as the parent fixture.
 * Ordinary task remains terminated with retained binding for lifetime checks. */
static struct kernel_task_handle prepare_supervisor(struct fixture *f)
{
    setup(f);
    assert(kernel_task_terminate(&f->tasks,f->current)==KERNEL_TASK_OK);
    struct kernel_task_handle supervisor={0};
    assert(kernel_task_create(&f->tasks,true,&supervisor)==KERNEL_TASK_OK);
    struct kernel_task_stack_binding binding={UINT64_C(0x110000),16384u,
        UINT64_C(0x113fe0),1u};
    assert(kernel_task_resources_bind(&f->resources,&f->tasks,supervisor,&binding)==KERNEL_RESOURCE_OK);
    assert(kernel_task_make_runnable(&f->tasks,supervisor)==KERNEL_TASK_OK);
    struct kernel_task_handle selected={0};
    assert(kernel_task_dispatch(&f->tasks,&selected)==KERNEL_TASK_OK);
    assert(selected.slot==supervisor.slot);
    f->cpu.stack=UINT64_C(0x200000);
    assert(d59b_memory_host_backend(&f->backend));
    return supervisor;
}
/* Every sticky negative runs in a separate process; ordinary code has no
 * ability to reset failure, change boot CPU or rebind a retired domain. */
int main(int argc,char **argv)
{
    assert(argc==2);
    struct fixture f;
    struct kernel_task_handle supervisor=prepare_supervisor(&f);
    if(strcmp(argv[1],"boot-if")==0) {
        f.cpu.enabled=true;
        assert(!d59b_memory_bootstrap() && !f.cpu.enabled);
        assert(!d59b_memory_bootstrap());
        puts("SCOPE_BOOT_IF_DENIED");return 0;
    }
    assert(d59b_memory_bootstrap());
    struct d59b_memory_scope boot={0},nested={0};
    f.cpu.enabled=true;
    assert(d59b_memory_scope_enter(&boot) && !f.cpu.enabled);
    assert(d59b_memory_scope_enter(&nested) && !f.cpu.enabled);
    assert(d59b_memory_boot_only());
    assert(d59b_memory_scope_leave(&nested) && !f.cpu.enabled);
    assert(d59b_memory_scope_leave(&boot) && f.cpu.enabled);
    f.cpu.enabled=false;
    if(strcmp(argv[1],"bind-stack")==0) {
        f.cpu.stack=UINT64_C(0x112000);
        assert(!d59b_memory_bind(&f.context));
        assert(!d59b_memory_is_bound(&f.context) && !f.cpu.enabled);
        puts("SCOPE_BIND_STACK_DENIED");return 0;
    }
    assert(d59b_memory_bind(&f.context));
    f.cpu.stack=UINT64_C(0x112000);
    assert(d59b_memory_is_bound(&f.context));
    struct d59b_memory_scope out={0};
    if(strcmp(argv[1],"irq")==0 || strcmp(argv[1],"cpu")==0 ||
       strcmp(argv[1],"rsp")==0) {
        if(strcmp(argv[1],"irq")==0) d59b_irq_depth=1;
        if(strcmp(argv[1],"cpu")==0) ++f.cpu.id;
        if(strcmp(argv[1],"rsp")==0) f.cpu.stack=UINT64_C(0x200000);
        f.cpu.enabled=true;
        struct d59b_memory_scope before=out;
        assert(!d59b_memory_scope_enter(&out));
        assert(memcmp(&before,&out,sizeof out)==0 && !f.cpu.enabled && f.guard.faulted);
        assert(!d59b_memory_is_bound(&f.context));
        puts("SCOPE_EXECUTION_DENIED");return 0;
    }
    if(strcmp(argv[1],"early-unbind")==0) {
        assert(!d59b_memory_unbind(&f.context) && f.guard.faulted && !f.cpu.enabled);
        puts("SCOPE_EARLY_UNBIND_DENIED");return 0;
    }
    if(strcmp(argv[1],"token-enable")==0 || strcmp(argv[1],"token-hide")==0) {
        f.cpu.enabled=strcmp(argv[1],"token-hide")==0;
        assert(d59b_memory_scope_enter(&out));
        out.previous_flags^=UINT64_C(0x200);
        struct d59b_memory_scope before=out;
        assert(!d59b_memory_scope_leave(&out));
        assert(memcmp(&before,&out,sizeof out)==0);
        assert(f.guard.faulted && f.guard.depth==1u && !f.cpu.enabled);
        puts("SCOPE_FORGED_IF_DENIED");return 0;
    }
    if(strcmp(argv[1],"cookie")==0 || strcmp(argv[1],"nested-if")==0) {
        f.cpu.enabled=true;
        assert(d59b_memory_scope_enter(&out));
        if(strcmp(argv[1],"cookie")==0) {
            ++out.cookie;
            struct d59b_memory_scope before=out;
            assert(!d59b_memory_scope_leave(&out));
            assert(memcmp(&before,&out,sizeof out)==0);
        } else {
            f.cpu.enabled=true;
            struct d59b_memory_scope other={0},before=other;
            assert(!d59b_memory_scope_enter(&other));
            assert(memcmp(&before,&other,sizeof other)==0);
        }
        assert(f.guard.faulted && f.guard.depth==1u && !f.cpu.enabled);
        puts("SCOPE_COOKIE_IF_DENIED");return 0;
    }
    assert(strcmp(argv[1],"happy")==0 || strcmp(argv[1],"retire-stack")==0);
    for(unsigned i=0;i<20000u;++i) {
        f.cpu.enabled=(i&1u)!=0;
        const bool enabled=f.cpu.enabled;
        struct d59b_memory_scope outer={0},inner={0};
        assert(d59b_memory_scope_enter(&outer));
        assert(d59b_memory_scope_enter(&inner));
        assert(f.guard.depth==2u && !f.cpu.enabled && !d59b_memory_boot_only());
        assert(d59b_memory_frame_bound(UINT64_C(0x100000)));
        assert(d59b_memory_frame_bound(UINT64_C(0x110000)));
        assert(!d59b_memory_frame_bound(UINT64_C(0x120000)));
        assert(!d59b_memory_frame_release_allowed(UINT64_C(0x110000)));
        assert(!d59b_memory_frame_release_allowed(UINT64_C(0x100000)));
        assert(d59b_memory_frame_release_allowed(UINT64_C(0x120000)));
        assert(d59b_memory_scope_leave(&inner) && !f.cpu.enabled);
        assert(d59b_memory_scope_leave(&outer));
        assert(f.cpu.enabled==enabled && f.guard.depth==0 && !f.guard.faulted);
    }
    /* Mixed direct adapter and wrapper nesting must preserve the outer IF. */
    f.cpu.enabled=true;
    uint32_t cookie=0;
    assert(d59b_enter(&f.context,&cookie));
    struct d59b_memory_scope inner={0};
    assert(d59b_memory_scope_enter(&inner));
    assert(d59b_memory_scope_leave(&inner) && !f.cpu.enabled);
    assert(d59b_exit(&f.context,cookie) && f.cpu.enabled);
    f.cpu.enabled=false;
    assert(kernel_task_terminate(&f.tasks,supervisor)==KERNEL_TASK_OK);
    f.cpu.stack=UINT64_C(0x200000);
    assert(d59b_memory_unbind(&f.context));
    if(strcmp(argv[1],"retire-stack")==0) {
        f.cpu.stack=UINT64_C(0x112000);
        struct d59b_memory_scope bad={0},before=bad;
        assert(!d59b_memory_scope_enter(&bad));
        assert(memcmp(&before,&bad,sizeof bad)==0 && !f.cpu.enabled);
        puts("SCOPE_RETIRED_STACK_DENIED");return 0;
    }
    struct d59b_memory_scope retire={0};
    assert(d59b_memory_scope_enter(&retire) && d59b_memory_boot_only());
    assert(d59b_memory_frame_release_allowed(UINT64_C(0x100000)));
    assert(d59b_memory_frame_release_allowed(UINT64_C(0x110000)));
    assert(kernel_task_resources_retire(&f.resources,&f.tasks,f.current,
        f.cpu.stack,release_owner)==KERNEL_RESOURCE_OK);
    assert(kernel_task_resources_retire(&f.resources,&f.tasks,supervisor,
        f.cpu.stack,release_owner)==KERNEL_RESOURCE_OK);
    assert(released==2u);
    assert(d59b_memory_scope_leave(&retire));
    assert(d59b_memory_retirement_complete(&f.context));
    assert(!d59b_memory_is_bound(&f.context));
    puts("SPIFFOS_M10_D59B_MEMORY_SCOPE_HOSTED_OK");
    return 0;
}
