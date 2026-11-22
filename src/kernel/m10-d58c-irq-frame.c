/* Purpose: D5.8c C1 immutable lock-bearing x86 IRQ frame validation.
 * Responsibilities: independently check physical stack bounds, complete
 * interrupt-return geometry, segments, IF and exact D5.3 ledger identity.
 * Dependencies: m10-d58c-irq-frame.h and M10-D2 fixed frame layout.
 * Invariants: never modify lock, task, ledger, frame, EOI or CPU flags;
 * no untrusted frame access before full pointer range validation.
 * Concurrency: input is a serialized IF-off IRQ0 state snapshot.
 * Reference: ADR-m10-d58c-real-irq-lock-owner.md and AGENTS.md. */
#include "m10-d58c-irq-frame.h"
#include "m10-d2-irq-frame.h"
#include <stddef.h>

/* Check full length using subtraction, not pointer+length, to prevent
 * overflow or unauthorized dereference past the bound owner's stack. */
static bool span(uintptr_t bottom,size_t capacity,
                 uintptr_t pointer,size_t bytes)
{
    if(bytes==0 || capacity<bytes || bottom>UINTPTR_MAX-capacity ||
       pointer<bottom) return false;
    return pointer-bottom<=capacity-bytes;
}

/* Four-level QEMU paging: sign-extend bit 47 into high 16 address bits.
 * LA57 and CR3 transitions are not part of this single-CPU diagnostic. */
static bool canonical(uint64_t address)
{
    return (address>>48)==(((address>>47)&1u)!=0 ?
        UINT64_C(0xffff):UINT64_C(0));
}

/* This is an additional check, not a relaxation of the pre-existing D5.6
 * source/target admission: only a D5.8c flagged guest invokes this path.
 * A passing graph is necessary but NOT sufficient without frame geometry. */
bool d58c_irq_frame_eligible(const struct kernel_task_table *tasks,
    const struct kernel_task_resources *resources,
    const struct d53_sync *sync,struct kernel_task_handle identity,
    enum kernel_task_state required,const uint64_t *frame,
    uint16_t expected_cs,uint16_t expected_ss,
    bool irq_exit,bool live_if_disabled,bool guard_clear)
{
    if(!irq_exit || !live_if_disabled || !guard_clear || frame==NULL ||
       identity.slot>=KERNEL_TASK_SUPERVISOR_SLOT || identity.generation==0 ||
       !d58b_lock_frame_eligible(tasks,resources,sync,identity,
                                required,true,true,true,true)) return false;
    const struct kernel_task_resource_record *owner=&resources->task[identity.slot];
    const uintptr_t base=owner->stack.bottom, ptr=(uintptr_t)frame;
    const size_t bytes=M10_D2_FRAME_QWORDS*sizeof(uint64_t);
    if((ptr&(uintptr_t)7u)!=0 ||
       !span(base,owner->stack.bytes,ptr,bytes) ||
       !span(base,owner->stack.bytes,owner->stack.saved_rsp,1))
        return false;
    /* Only now is the frame fully covered by the generation-owned memory. */
    const uint64_t rip=frame[M10_D2_RIP];
    const uint64_t rsp=frame[M10_D2_RSP];
    const uint64_t flags=frame[M10_D2_RFLAGS];
    if(!canonical(rip) || !canonical(rsp) ||
       (flags&UINT64_C(0x202))!=UINT64_C(0x202) ||
       frame[M10_D2_CS]!=expected_cs ||
       frame[M10_D2_SS]!=expected_ss ||
       !span(base,owner->stack.bytes,(uintptr_t)rsp,1))
        return false;
    /* Source/target saved application stack must be above all 20 qwords
     * of the interrupt frame; no synthesized overlap is acceptable. */
    return rsp>ptr && rsp-ptr>=bytes;
}
