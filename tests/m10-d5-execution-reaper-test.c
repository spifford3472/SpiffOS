/* Purpose: M10-D5.2a hosted execution-aware reaper stress and negative tests.
 * Dependencies: kernel-task and kernel-task-reaper pure metadata modules.
 * Invariants: no release on live stack, bad context, wrong generation or
 * callback failure; exactly one release then descriptor reuse.
 * Concurrency: hosted single CPU; test models caller-owned IF=0 exclusion.
 * References: ADR-m10-d5-execution-aware-reaper.md and AGENTS.md. */
#include "../src/kernel/kernel-task-reaper.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned releases;
static size_t last_owner;
static bool refuse;

/* Memory-owner substitute: returns failure without releasing anything. */
static bool release_stack(size_t owner)
{
    if (refuse) return false;
    ++releases;
    last_owner = owner;
    return true;
}

/* Make an actually TERMINATED record, preserving generation-tagged ID. */
static struct kernel_task_handle terminated(struct kernel_task_table *table)
{
    struct kernel_task_handle id;
    assert(kernel_task_create(table, false, &id) == KERNEL_TASK_OK);
    assert(kernel_task_make_runnable(table, id) == KERNEL_TASK_OK);
    struct kernel_task_handle running;
    assert(kernel_task_dispatch(table, &running) == KERNEL_TASK_OK);
    assert(running.slot == id.slot && running.generation == id.generation);
    assert(kernel_task_terminate(table, id) == KERNEL_TASK_OK);
    return id;
}

/* Refusals preserve both task metadata and memory-owner release count. */
static void should_reject(struct kernel_task_table *table,
                          struct kernel_task_handle id,
                          const struct kernel_task_stack_binding *stack,
                          uintptr_t executing_rsp,
                          enum kernel_task_result expected)
{
    struct kernel_task_table snapshot = *table;
    unsigned before = releases;
    assert(kernel_task_reap_owned(table, id, stack, executing_rsp,
                                  release_stack) == expected);
    assert(releases == before);
    assert(memcmp(table, &snapshot, sizeof snapshot) == 0);
}

static void rejection_cases(void)
{
    struct kernel_task_table table = {0};
    assert(kernel_task_init(&table));
    struct kernel_task_handle id = terminated(&table);
    struct kernel_task_stack_binding stack = {
        .bottom = UINTPTR_C(0x100000),
        .bytes = 16384,
        .saved_rsp = UINTPTR_C(0x103fe0),
        .owner_index = 7
    };
    uintptr_t dispatcher_rsp = UINTPTR_C(0x200000);
    assert(kernel_task_stack_safe_to_release(&stack, dispatcher_rsp));
    should_reject(&table, id, &stack, stack.bottom + 128,
                  KERNEL_TASK_BAD_STATE); /* self-free */
    stack.saved_rsp = stack.bottom - 8;
    should_reject(&table, id, &stack, dispatcher_rsp,
                  KERNEL_TASK_BAD_STATE);
    stack.saved_rsp = stack.bottom + stack.bytes - 8;
    stack.bottom = UINTPTR_MAX - 4095;
    should_reject(&table, id, &stack, dispatcher_rsp,
                  KERNEL_TASK_BAD_STATE);
    stack.bottom = UINTPTR_C(0x100000);
    stack.saved_rsp = UINTPTR_C(0x103fe0);
    stack.bytes = 64;
    should_reject(&table, id, &stack, dispatcher_rsp,
                  KERNEL_TASK_BAD_STATE);
    stack.bytes = 16384;
    stack.saved_rsp = stack.bottom + 3;
    should_reject(&table, id, &stack, dispatcher_rsp,
                  KERNEL_TASK_BAD_STATE);
    stack.saved_rsp = UINTPTR_C(0x103fe0);
    refuse = true;
    should_reject(&table, id, &stack, dispatcher_rsp,
                  KERNEL_TASK_BAD_STATE);
    refuse = false;
    assert(kernel_task_reap_owned(&table, id, &stack, dispatcher_rsp,
                                  release_stack) == KERNEL_TASK_OK);
    assert(releases == 1 && last_owner == 7);
    should_reject(&table, id, &stack, dispatcher_rsp,
                  KERNEL_TASK_BAD_STATE);
    struct kernel_task_handle replacement;
    assert(kernel_task_create(&table, false, &replacement) == KERNEL_TASK_OK);
    assert(replacement.slot == id.slot && replacement.generation != id.generation);
    should_reject(&table, id, &stack, dispatcher_rsp,
                  KERNEL_TASK_BAD_HANDLE);
}

/* Many retirement cycles prove no double-free or stale handle aliasing. */
static void stress_reclamation(void)
{
    struct kernel_task_table table = {0};
    assert(kernel_task_init(&table));
    unsigned start_releases = releases;
    for (unsigned i = 0; i < 20000; ++i) {
        struct kernel_task_handle id = terminated(&table);
        struct kernel_task_stack_binding stack = {
            .bottom = UINTPTR_C(0x400000),
            .bytes = 16384,
            .saved_rsp = UINTPTR_C(0x403fe0),
            .owner_index = i % 2
        };
        assert(kernel_task_reap_owned(&table, id, &stack,
                   UINTPTR_C(0x500000), release_stack) == KERNEL_TASK_OK);
        assert(kernel_task_check(&table));
    }
    assert(releases - start_releases == 20000);
}

int main(void)
{
    rejection_cases();
    stress_reclamation();
    puts("SPIFFOS_M10_D5_EXECUTION_REAPER_OK");
    return 0;
}
