/* Purpose: M10-D5.1 hosted reclamation and stale-handle stress tests.
 * Dependencies: kernel-task.h; hosted C assertions only.
 * Invariants: terminated slots cannot be reused before explicit reaping;
 * a reclaimed slot requires generation advancement; no IRQ/hardware.
 * Reference: docs/decisions/ADR-m10-d5-production-task-model.md. */
#include "../src/kernel/kernel-task.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Verify failed operations are pure and do not corrupt a task table. */
static void expect_unchanged(const struct kernel_task_table *table,
                             const struct kernel_task_table *before)
{
    assert(memcmp(table, before, sizeof *table) == 0);
    assert(kernel_task_check(table));
}

/* Exercise reuse cycles without ever allowing an obsolete handle to work. */
static void cycle_reclamation(void)
{
    struct kernel_task_table table = {0};
    assert(kernel_task_init(&table));
    struct kernel_task_handle old = {UINT32_MAX, 0};
    for (unsigned n = 0; n < 20000; ++n) {
        struct kernel_task_handle h;
        assert(kernel_task_create(&table, false, &h) == KERNEL_TASK_OK);
        assert(h.slot == 0);
        if (n != 0) assert(h.generation != old.generation);
        assert(kernel_task_make_runnable(&table, h) == KERNEL_TASK_OK);
        struct kernel_task_handle active;
        assert(kernel_task_dispatch(&table, &active) == KERNEL_TASK_OK);
        assert(active.slot == h.slot && active.generation == h.generation);
        assert(kernel_task_terminate(&table, h) == KERNEL_TASK_OK);
        assert(kernel_task_get(&table, h) != NULL);
        assert(!kernel_task_get(&table, h)->reclaimed);
        assert(kernel_task_check(&table));
        assert(kernel_task_reap(&table, h) == KERNEL_TASK_OK);
        assert(kernel_task_get(&table, h)->reclaimed);
        struct kernel_task_table before = table;
        assert(kernel_task_reap(&table, h) == KERNEL_TASK_BAD_STATE);
        expect_unchanged(&table, &before);
        old = h;
    }
}

/* Fill the table to ensure unreaped tasks cannot take another slot or reserve. */
static void reject_reuse_before_cleanup(void)
{
    struct kernel_task_table table = {0};
    struct kernel_task_handle handles[KERNEL_TASK_SUPERVISOR_SLOT], super;
    assert(kernel_task_init(&table));
    for (unsigned i = 0; i < KERNEL_TASK_SUPERVISOR_SLOT; ++i)
        assert(kernel_task_create(&table, false, &handles[i]) == KERNEL_TASK_OK);
    assert(kernel_task_create(&table, true, &super) == KERNEL_TASK_OK);
    assert(kernel_task_terminate(&table, handles[0]) == KERNEL_TASK_OK);
    assert(kernel_task_terminate(&table, super) == KERNEL_TASK_OK);
    struct kernel_task_table before = table;
    struct kernel_task_handle output = {100, 100};
    assert(kernel_task_create(&table, false, &output) == KERNEL_TASK_NO_CAPACITY);
    expect_unchanged(&table, &before);
    assert(output.slot == 100 && output.generation == 100);
    assert(kernel_task_create(&table, true, &output) == KERNEL_TASK_NO_CAPACITY);
    expect_unchanged(&table, &before);
    assert(kernel_task_reap(&table, handles[1]) == KERNEL_TASK_BAD_STATE);
    expect_unchanged(&table, &before);
    assert(kernel_task_reap(&table, handles[0]) == KERNEL_TASK_OK);
    assert(kernel_task_reap(&table, super) == KERNEL_TASK_OK);
    assert(kernel_task_create(&table, false, &output) == KERNEL_TASK_OK);
    assert(output.slot == handles[0].slot && output.generation != handles[0].generation);
    before = table;
    assert(kernel_task_reap(&table, handles[0]) == KERNEL_TASK_BAD_HANDLE);
    expect_unchanged(&table, &before);
    assert(kernel_task_create(&table, true, &output) == KERNEL_TASK_OK);
    assert(output.slot == KERNEL_TASK_SUPERVISOR_SLOT);
}

/* Disallow a superficially valid descriptor carrying contradictory metadata. */
static void corrupt_reclamation(void)
{
    struct kernel_task_table table = {0};
    struct kernel_task_handle h;
    assert(kernel_task_init(&table));
    assert(kernel_task_create(&table, false, &h) == KERNEL_TASK_OK);
    table.tasks[h.slot].reclaimed = true;
    assert(!kernel_task_check(&table));
    struct kernel_task_table before = table;
    assert(kernel_task_reap(&table, h) == KERNEL_TASK_CORRUPT);
    assert(memcmp(&table, &before, sizeof table) == 0);
}

int main(void)
{
    cycle_reclamation();
    reject_reuse_before_cleanup();
    corrupt_reclamation();
    puts("SPIFFOS_M10_D5_REAP_OK");
    return 0;
}
