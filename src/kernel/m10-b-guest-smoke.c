/* Purpose: QEMU-only M10-B cooperative switch and owned-stack probe.
 * Dependencies: x86-task-context, memory-boot M2/M3 verified 4-frame stacks.
 * Invariants: every task stack is ordinary KERNEL owned, contiguous, zeroed,
 * and released only after task execution returns to the surviving boot stack.
 * IRQ restrictions: save/restore prior IF; no M2/M3 allocation with IF=1;
 * real PIT0 IRQ0 can interrupt task execution but never switches context.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "x86-task-context.h"
#include "memory-boot.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SPIFFOS_M10_B_SWITCH_TEST
enum { TEST_ROUNDS = 512, TEST_STACKS = 2 };
static struct x86_task_context boot_context;
static struct x86_task_context task_context[TEST_STACKS];
static uint8_t *task_stacks[TEST_STACKS];
static volatile uint32_t task_step[TEST_STACKS];
static volatile bool task_done[TEST_STACKS];
static volatile bool task_failed;
static const char *failure_reason = "UNSET";

/* Borrow a static label for serial logging by the original boot stack. */
const char *m10_b_guest_failure_reason(void) { return failure_reason; }

/* Save RFLAGS, then disable interrupts for exclusive M2/M3 ownership edits.
 * Restore exactly previous IF; never blindly enable an originally disabled
 * interrupt state. All other RFLAGS are left to normal C ABI semantics. */
static uint64_t save_and_disable_irq(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void restore_irq(uint64_t flags)
{
    if((flags & UINT64_C(0x200))!=0)
        __asm__ volatile ("sti" : : : "memory");
}

/* First-run trampoline never returns, and always returns to the scheduler
 * stack after each yield. A completed task cannot be scheduled again. */
__attribute__((noreturn))
static void task_one(void)
{
    for(uint32_t i=0;i<TEST_ROUNDS;++i) {
        if(task_step[0]!=i) task_failed=true;
        task_step[0]=i+1;
        x86_task_switch(&task_context[0],&boot_context);
    }
    task_done[0]=true;
    x86_task_switch(&task_context[0],&boot_context);
    for(;;) __asm__ volatile("cli; hlt" : : : "memory");
}

/* Independent entry and saved callee registers on separately owned pages. */
__attribute__((noreturn))
static void task_two(void)
{
    for(uint32_t i=0;i<TEST_ROUNDS;++i) {
        if(task_step[1]!=i) task_failed=true;
        task_step[1]=i+1;
        x86_task_switch(&task_context[1],&boot_context);
    }
    task_done[1]=true;
    x86_task_switch(&task_context[1],&boot_context);
    for(;;) __asm__ volatile("cli; hlt" : : : "memory");
}

/* Real QEMU guest is already in a functioning M9-3D6 timer environment.
 * Initialize stack ownership only under IF=0, run entirely cooperatively,
 * then reclaim under IF=0 after returning to the bootstrap stack. */
bool m10_b_guest_smoke(bool (*read_clock_ms)(uint64_t *))
{
    if(read_clock_ms==NULL) {
        failure_reason="CLOCK_CALLBACK_MISSING";
        return false;
    }
    uint64_t flags=save_and_disable_irq();
    if((flags & UINT64_C(0x200))==0) {
        failure_reason="INTERRUPTS_NOT_ENABLED";
        return false;
    }
    void *owned0=NULL,*owned1=NULL;
    bool prepared0=memory_boot_m10_task_stack_prepare(0,&owned0);
    bool prepared1=prepared0 &&
        memory_boot_m10_task_stack_prepare(1,&owned1);
    if(!prepared1 && prepared0)
        (void)memory_boot_m10_task_stack_release(0);
    restore_irq(flags);
    if(!prepared0 || !prepared1) {
        failure_reason="OWNED_STACK_PREPARE";
        return false;
    }
    task_stacks[0]=(uint8_t *)owned0;
    task_stacks[1]=(uint8_t *)owned1;
    bool success=false;
    for(size_t task=0;task<TEST_STACKS;++task)
        for(size_t byte=0;byte<X86_TASK_STACK_BYTES;++byte)
            task_stacks[task][byte]=UINT8_C(0x5a);
    if(!x86_task_context_init(&task_context[0],task_stacks[0],
                                X86_TASK_STACK_BYTES,task_one) ||
       !x86_task_context_init(&task_context[1],task_stacks[1],
                                X86_TASK_STACK_BYTES,task_two)) {
        failure_reason="CONTEXT_INIT";
        goto cleanup;
    }
    uint64_t first_tick=0,last_tick=0;
    bool clock_available=read_clock_ms(&first_tick);
    for(uint32_t round=0;round<TEST_ROUNDS;++round) {
        x86_task_switch(&boot_context,&task_context[0]);
        x86_task_switch(&boot_context,&task_context[1]);
        if(task_failed || task_step[0]!=round+1 ||
           task_step[1]!=round+1) {
            failure_reason="PROGRESS";
            goto cleanup;
        }
    }
    x86_task_switch(&boot_context,&task_context[0]);
    x86_task_switch(&boot_context,&task_context[1]);
    if(!task_done[0] || !task_done[1] ||
       (clock_available &&
        (!read_clock_ms(&last_tick) || last_tick<first_tick))) {
        failure_reason="TERMINATION_OR_CLOCK";
        goto cleanup;
    }
    for(size_t task=0;task<TEST_STACKS;++task)
        for(size_t byte=0;byte<64;++byte)
            if(task_stacks[task][byte]!=UINT8_C(0x5a)) {
                failure_reason="STACK_CANARY";
                goto cleanup;
            }
    success=true;
cleanup:
    /* Task exit has already returned to the boot stack. Releasing either
     * frame group while executing on it would be a use-after-free. */
    flags=save_and_disable_irq();
    bool released1=memory_boot_m10_task_stack_release(1);
    bool released0=memory_boot_m10_task_stack_release(0);
    restore_irq(flags);
    if(!released0 || !released1) {
        failure_reason="OWNED_STACK_RELEASE";
        return false;
    }
    return success;
}
#endif
