/* Purpose: M10-B QEMU-only cooperative context-switch execution probe.
 * Dependencies: x86-task-context.h and dedicated x86-task-switch.S.
 * Invariants: two static test stacks never escape this module; the test
 * returns to the boot stack for diagnostics and never frees a running stack.
 * This is a bounded first switch proof, NOT production task stack allocation.
 * IRQ context: no switch in IRQ0, caller has enabled safe PIT0 route.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "x86-task-context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SPIFFOS_M10_B_SWITCH_TEST
enum { TEST_ROUNDS = 512 };
static struct x86_task_context boot_context;
static struct x86_task_context task_context[2];
static uint8_t task_stacks[2][X86_TASK_STACK_BYTES]
    __attribute__((aligned(16)));
static volatile uint32_t task_step[2];
static volatile bool task_done[2];
static volatile bool task_failed;
static bool (*guest_clock)(uint64_t *);
static uint64_t first_tick, last_tick;
static bool clock_available_at_entry;
static const char *failure_reason = "UNSET";

/* Test-only borrowed static error string for bounded serial diagnostics. */
const char *m10_b_guest_failure_reason(void) { return failure_reason; }

/* Each task validates its caller-independent progress and saved stack
 * guards, then yields to the persistent root stack. It never returns. */
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
    for(;;) __asm__ volatile("hlt");
}

/* Same checks on completely disjoint stack and context storage. */
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
    for(;;) __asm__ volatile("hlt");
}

/* Invoked only by test-specific kernel branch after real IRQ0 activation.
 * Restrict all operations to preinitialized, static 16KiB stacks until B
 * receives its separate physical-frame ownership/cleanup implementation. */
bool m10_b_guest_smoke(bool (*read_clock_ms)(uint64_t *))
{
    if(read_clock_ms==NULL) {
        failure_reason="CLOCK_CALLBACK_MISSING";
        return false;
    }
    guest_clock=read_clock_ms;
    /* M9 timer is intentionally unavailable until its first hardware IRQ.
     * A fast switch probe must not invent a tick or mistake that for failure. */
    clock_available_at_entry=guest_clock(&first_tick);
    for(size_t task=0;task<2;++task)
        for(size_t byte=0;byte<X86_TASK_STACK_BYTES;++byte)
            task_stacks[task][byte]=UINT8_C(0x5a);
    if(!x86_task_context_init(&task_context[0],task_stacks[0],
                                sizeof task_stacks[0],task_one) ||
       !x86_task_context_init(&task_context[1],task_stacks[1],
                                sizeof task_stacks[1],task_two)) {
        failure_reason="STACK_INIT";
        return false;
    }
    for(uint32_t round=0;round<TEST_ROUNDS;++round) {
        x86_task_switch(&boot_context,&task_context[0]);
        x86_task_switch(&boot_context,&task_context[1]);
        if(task_failed || task_step[0]!=round+1 ||
           task_step[1]!=round+1) {
            failure_reason="PROGRESS";
            return false;
        }
    }
    x86_task_switch(&boot_context,&task_context[0]);
    x86_task_switch(&boot_context,&task_context[1]);
    if(!task_done[0] || !task_done[1] ||
       (clock_available_at_entry &&
        (!guest_clock(&last_tick) || last_tick<first_tick))) {
        failure_reason="FINAL_STATE_OR_CLOCK";
        return false;
    }
    for(size_t task=0;task<2;++task)
        for(size_t byte=0;byte<64;++byte)
            if(task_stacks[task][byte]!=UINT8_C(0x5a)) {
                failure_reason="STACK_CANARY";
                return false;
            }
    return true;
}
#endif
