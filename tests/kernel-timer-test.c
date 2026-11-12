/* Host-only timer arithmetic, availability, saturation and error tests.
 * No privileged interrupts, wall clock, QEMU, allocation or device IO. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "kernel-timer.h"

/* Exercise the full state machine and arithmetic boundaries deterministically.
 * One test owns all state; false paths preserve caller's output sentinel. */
int main(void)
{
    struct kernel_timer timer = {0};
    uint64_t out = UINT64_C(0x123456789);
    assert(!kernel_timer_init(NULL, 100));
    assert(!kernel_timer_init(&timer, 0));
    assert(memcmp(&timer, &(struct kernel_timer){0}, sizeof timer) == 0);
    assert(!kernel_timer_note_tick(&timer));
    assert(!kernel_timer_read_ticks(&timer, &out) && out == UINT64_C(0x123456789));
    assert(kernel_timer_init(&timer, 100));
    assert(!kernel_timer_init(&timer, 200));
    assert(!kernel_timer_read_ticks(&timer, &out) && out == UINT64_C(0x123456789));
    assert(kernel_timer_note_tick(&timer));
    assert(kernel_timer_read_ticks(&timer, &out) && out == 1);
    assert(kernel_timer_ticks_to_ms(out, 100, &out) && out == 10);
    for (int i = 0; i < 99; ++i)
        assert(kernel_timer_note_tick(&timer));
    assert(kernel_timer_read_ticks(&timer, &out) && out == 100);
    assert(kernel_timer_ticks_to_ms(out, 100, &out) && out == 1000);
    out = 678;
    assert(!kernel_timer_ticks_to_ms(4, 0, &out) && out == 678);
    assert(!kernel_timer_ticks_to_ms(4, 1, NULL));
    assert(kernel_timer_ticks_to_ms(1, 3, &out) && out == 333);
    assert(kernel_timer_ticks_to_ms(2, 3, &out) && out == 666);
    assert(kernel_timer_ticks_to_ms(UINT64_MAX, 100, &out) && out == UINT64_MAX);
    assert(kernel_timer_ticks_to_ms(UINT64_MAX, UINT32_MAX, &out) &&
           out > 0 && out < UINT64_MAX);
    out = 678;
    assert(!kernel_timer_elapsed_ticks(1000, 10, &out) && out == 678);
    assert(!kernel_timer_elapsed_ticks(0, 1, NULL));
    assert(kernel_timer_elapsed_ticks(123, 123, &out) && out == 0);
    assert(kernel_timer_elapsed_ticks(0, UINT64_MAX, &out) &&
           out == UINT64_MAX);
    /* Verify explicit saturation at the last representable tick. */
    __atomic_store_n(&timer.ticks, UINT64_MAX - 1u, __ATOMIC_RELAXED);
    assert(kernel_timer_note_tick(&timer));
    assert(kernel_timer_read_ticks(&timer, &out) && out == UINT64_MAX);
    assert(kernel_timer_note_tick(&timer));
    assert(kernel_timer_read_ticks(&timer, &out) && out == UINT64_MAX);
    assert(!kernel_timer_read_ticks(&timer, NULL));
    return 0;
}
