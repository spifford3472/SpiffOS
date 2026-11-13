/* Purpose: QEMU-only M10-B cooperative switch and owned-stack probe.
 * Dependencies: x86-task-context, memory-boot M2/M3 verified 4-frame stacks.
 * Invariants: every task stack is ordinary KERNEL owned, contiguous, zeroed,
 * and released only after task execution returns to the surviving boot stack.
 * IRQ restrictions: save/restore prior IF; no M2/M3 allocation with IF=1;
 * real PIT0 IRQ0 can interrupt task execution but never switches context.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "x86-task-context.h"
#include "memory-boot.h"
#include "kernel-task.h"
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
static struct kernel_task_table task_table;
static struct kernel_task_handle task_handles[TEST_STACKS];
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

/* Relinquish execution through policy BEFORE switching stacks. The queue
 * edit is serialized with IF=0 but IRQ0 is restored before the switch. */
static void guest_task_yield(size_t index)
{
    uint64_t flags=save_and_disable_irq();
    enum kernel_task_result result=kernel_task_yield(
        &task_table,task_handles[index]);
    restore_irq(flags);
    if(result!=KERNEL_TASK_OK) task_failed=true;
}

/* Exit removes the current descriptor; stack reclamation happens later
 * on the boot stack after all contexts have transferred out. */
static void guest_task_exit(size_t index)
{
    uint64_t flags=save_and_disable_irq();
    enum kernel_task_result result=kernel_task_terminate(
        &task_table,task_handles[index]);
    restore_irq(flags);
    if(result!=KERNEL_TASK_OK) task_failed=true;
}

/* First-run trampoline never returns, and always returns to the scheduler
 * stack after each yield. A completed task cannot be scheduled again. */
__attribute__((noreturn))
static void task_one(void)
{
    for(uint32_t i=0;i<TEST_ROUNDS;++i) {
        if(task_step[0]!=i) task_failed=true;
        task_step[0]=i+1;
        guest_task_yield(0);
        x86_task_switch(&task_context[0],&boot_context);
    }
    guest_task_exit(0);
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
        guest_task_yield(1);
        x86_task_switch(&task_context[1],&boot_context);
    }
    guest_task_exit(1);
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
    /* Prove that an injected failure after two allocations completely
     * restores ordinary, kernel and supervisor-reserve accounting. */
    if(!memory_boot_m10_task_stack_failure_probe()) {
        restore_irq(flags);
        failure_reason="PARTIAL_ROLLBACK";
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
    flags=save_and_disable_irq();
    bool policy_ready=kernel_task_init(&task_table);
    for(size_t i=0;i<TEST_STACKS && policy_ready;++i)
        policy_ready=kernel_task_create(&task_table,false,
                         &task_handles[i])==KERNEL_TASK_OK &&
                     kernel_task_make_runnable(&task_table,
                         task_handles[i])==KERNEL_TASK_OK;
    restore_irq(flags);
    if(!policy_ready) {
        failure_reason="POLICY_INIT";
        goto cleanup;
    }
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
    /* The dispatcher, not a fixed test ordering, selects all 1026
     * context transfers. FIFO fairness should naturally alternate slots. */
    for(uint32_t turn=0;turn<TEST_ROUNDS*TEST_STACKS+TEST_STACKS;++turn) {
        struct kernel_task_handle selected={0};
        flags=save_and_disable_irq();
        enum kernel_task_result chosen=kernel_task_dispatch(
            &task_table,&selected);
        restore_irq(flags);
        if(chosen!=KERNEL_TASK_OK || selected.slot>=TEST_STACKS) {
            failure_reason="DISPATCH";
            goto cleanup;
        }
        x86_task_switch(&boot_context,&task_context[selected.slot]);
        if(task_failed || !kernel_task_check(&task_table)) {
            failure_reason="POLICY_OR_PROGRESS";
            goto cleanup;
        }
        if(turn<TEST_ROUNDS*TEST_STACKS &&
           (selected.slot!=(turn%TEST_STACKS) ||
            task_step[selected.slot]!=(turn/TEST_STACKS)+1u)) {
            failure_reason="ROUND_ROBIN";
            goto cleanup;
        }
    }
    if(task_table.ready_count!=0 || task_table.current_slot!=SIZE_MAX) {
        failure_reason="NONEMPTY_AT_EXIT";
        goto cleanup;
    }
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
