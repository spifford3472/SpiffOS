/* Purpose: M10-C hosted scheduler tick accounting regressions.
 * Dependencies: kernel-scheduler-clock.h, assertions, ASan/UBSan.
 * Invariants: interval requests coalesce, no tick/phase wrap, rejected
 * calls preserve state, no context switch or hardware IRQ during test.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "../src/kernel/kernel-scheduler-clock.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Verify interval boundaries and that a pending request is a coalescing bit. */
static void intervals(void)
{
    struct kernel_scheduler_clock clock={0};
    assert(kernel_scheduler_clock_init(&clock,3));
    uint64_t ticks=99;
    assert(kernel_scheduler_clock_read_ticks(&clock,&ticks));
    assert(ticks==0);
    for(unsigned i=1;i<=2;++i) {
        assert(kernel_scheduler_clock_note_tick(&clock));
        assert(!kernel_scheduler_clock_take_request(&clock));
    }
    assert(kernel_scheduler_clock_note_tick(&clock));
    assert(kernel_scheduler_clock_read_ticks(&clock,&ticks) && ticks==3);
    assert(clock.phase_ticks==0);
    for(unsigned i=0;i<7;++i) assert(kernel_scheduler_clock_note_tick(&clock));
    assert(kernel_scheduler_clock_take_request(&clock));
    assert(!kernel_scheduler_clock_take_request(&clock));
    assert(kernel_scheduler_clock_read_ticks(&clock,&ticks) && ticks==10);
    assert(clock.phase_ticks==1);
    assert(kernel_scheduler_clock_note_tick(&clock));
    assert(!kernel_scheduler_clock_take_request(&clock));
    assert(kernel_scheduler_clock_note_tick(&clock));
    assert(kernel_scheduler_clock_take_request(&clock));
}

/* Rejected init/invalid note calls must preserve all bytes and outputs. */
static void invalids(void)
{
    struct kernel_scheduler_clock clock={0};
    struct kernel_scheduler_clock prior=clock;
    assert(!kernel_scheduler_clock_init(&clock,0));
    assert(memcmp(&clock,&prior,sizeof clock)==0);
    assert(!kernel_scheduler_clock_note_tick(&clock));
    assert(!kernel_scheduler_clock_take_request(&clock));
    uint64_t sample=47;
    assert(!kernel_scheduler_clock_read_ticks(&clock,&sample));
    assert(sample==47);
    assert(kernel_scheduler_clock_init(&clock,1));
    prior=clock;
    assert(!kernel_scheduler_clock_init(&clock,100));
    assert(memcmp(&clock,&prior,sizeof clock)==0);
    assert(kernel_scheduler_clock_note_tick(&clock));
    assert(kernel_scheduler_clock_take_request(&clock));
    assert(!kernel_scheduler_clock_read_ticks(&clock,NULL));
    assert(!kernel_scheduler_clock_note_tick(NULL));
    assert(!kernel_scheduler_clock_take_request(NULL));
}

/* No u64 wrap or artificial time requests past saturation. */
static void saturation(void)
{
    struct kernel_scheduler_clock clock={0};
    assert(kernel_scheduler_clock_init(&clock,2));
    clock.tick_count=UINT64_MAX-1;
    assert(kernel_scheduler_clock_note_tick(&clock));
    assert(clock.tick_count==UINT64_MAX);
    assert(kernel_scheduler_clock_note_tick(&clock));
    assert(clock.tick_count==UINT64_MAX);
    assert(!kernel_scheduler_clock_take_request(&clock));
    assert(kernel_scheduler_clock_init(NULL,1)==false);
    assert(kernel_scheduler_clock_boot_init(10));
    for(unsigned i=0;i<30;++i)
        assert(kernel_scheduler_clock_boot_note_tick());
    uint64_t result=0;
    assert(kernel_scheduler_clock_boot_read_ticks(&result) && result==30);
    assert(kernel_scheduler_clock_boot_take_request());
    assert(!kernel_scheduler_clock_boot_take_request());
}

/* A future M10-D scheduler may consume a request, but C must never invoke
 * a context-switch callback or mutate any runnable queue. */
int main(void)
{
    intervals();
    invalids();
    saturation();
    puts("SPIFFOS_M10_C_ACCOUNTING_OK");
    return 0;
}
