/* Purpose: hosted M10-B smoke proof for cooperative x86-64 stack switching.
 * Dependencies: x86-task-context.h, assembly, and host C assert.
 * Invariants: independent stack ownership, task continuation after yield,
 * first-run trampoline, no return from completed task and validated rejection.
 * This is NOT proof of kernel IRQ, physical-stack ownership or FPU safety.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "../src/kernel/x86-task-context.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct x86_task_context root, tasks[2];
static unsigned char stacks[2][X86_TASK_STACK_BYTES]
    __attribute__((aligned(16)));
static volatile unsigned int steps[2],done[2];
static unsigned char events[12];
static size_t event_count;

/* A task runs on its own persistent stack. It never returns from its
 * trampoline; after marking completion it transfers back to root. */
__attribute__((noreturn))
static void task_a(void)
{
    for(unsigned i=0;i<4;++i) {
        assert(steps[0]==i);
        events[event_count++]=1;
        steps[0]++;
        x86_task_switch(&tasks[0],&root);
    }
    done[0]=1;
    x86_task_switch(&tasks[0],&root);
    abort();
}

/* Task B uses entirely separate stack storage and local control flow. */
__attribute__((noreturn))
static void task_b(void)
{
    for(unsigned i=0;i<4;++i) {
        assert(steps[1]==i);
        events[event_count++]=2;
        steps[1]++;
        x86_task_switch(&tasks[1],&root);
    }
    done[1]=1;
    x86_task_switch(&tasks[1],&root);
    abort();
}

/* Validate both tasks progress in ordered alternating turns, then exit from
 * their own stacks without freeing the active stack. */
static void cooperative_execution(void)
{
    memset(stacks,0x5a,sizeof stacks);
    assert(x86_task_context_init(&tasks[0],stacks[0],
            sizeof stacks[0],task_a));
    assert(x86_task_context_init(&tasks[1],stacks[1],
            sizeof stacks[1],task_b));
    for(unsigned i=0;i<4;++i) {
        x86_task_switch(&root,&tasks[0]);
        x86_task_switch(&root,&tasks[1]);
        assert(event_count==2u*(i+1u));
    }
    x86_task_switch(&root,&tasks[0]);
    x86_task_switch(&root,&tasks[1]);
    assert(done[0] && done[1]);
    for(unsigned i=0;i<8;++i) assert(events[i]==(unsigned char)(1+i%2));
    for(size_t task=0;task<2;++task)
        for(size_t byte=0;byte<64;++byte)
            assert(stacks[task][byte]==0x5a);
}

/* Incorrect sizes, alignment and overlapping context must not touch memory. */
static void invalid_stack(void)
{
    struct x86_task_context context={0x1234};
    unsigned char scratch[X86_TASK_STACK_BYTES] __attribute__((aligned(16)));
    memset(scratch,0x77,sizeof scratch);
    assert(!x86_task_context_init(&context,scratch,
            sizeof scratch-1,task_a));
    assert(!x86_task_context_init(&context,scratch+1,
            sizeof scratch-1,task_a));
    assert(!x86_task_context_init(&context,scratch,
            sizeof scratch,NULL));
    assert(!x86_task_context_init(&context,scratch,0,task_a));
    assert(context.saved_rsp==0x1234);
    for(size_t i=0;i<sizeof scratch;++i) assert(scratch[i]==0x77);
}

int main(void)
{
    invalid_stack();
    cooperative_execution();
    puts("SPIFFOS_M10_B_HOST_SWITCH_OK");
    return 0;
}
