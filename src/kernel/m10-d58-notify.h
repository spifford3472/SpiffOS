/* Purpose: M10-D5.8a bounded diagnostic IRQ observation queue.
 * Responsibilities: capture exact-generation task/tick observations at IRQ
 * exit, and permit only a verified non-IRQ supervisor to consume them.
 * Dependencies: authoritative M10-A task table; no allocator or hardware.
 * Invariants: not a mutex command queue; never mutates task/lock state;
 * permanent fail-closed overflow, stale identity and corrupt-state latch.
 * Concurrency: one CPU, IF=0 for both operations; no nested IRQ producer.
 * Reference: ADR-m10-d58-irq-mutex-integration.md, AGENTS.md. */
#ifndef SPIFFOS_M10_D58_NOTIFY_H
#define SPIFFOS_M10_D58_NOTIFY_H
#include "kernel-task.h"

enum { D58_NOTICE_CAPACITY=8 };
struct d58_notice {
    struct kernel_task_handle observed;
    uint64_t tick;
};
struct d58_notice_queue {
    struct d58_notice records[D58_NOTICE_CAPACITY];
    size_t head, count;
    uint64_t latest_tick;
    bool initialized, faulted;
};

/* Initialize only unused zeroed caller-owned storage; no reset after fault. */
bool d58_queue_init(struct d58_notice_queue *queue);

/* Observe authentic currently RUNNING task from trusted IRQ-exit context.
 * No lock or task mutation. false latches fault on invalid input/full queue. */
bool d58_note_irq(struct d58_notice_queue *queue,
    const struct kernel_task_table *tasks,
    struct kernel_task_handle observed,uint64_t tick,
    bool irq_exit,bool live_if_disabled);

/* Consume only on verified non-IRQ supervisor safe point with IF=0;
 * generation/state rechecked before removal. Empty is ordinary false;
 * other invalid requests latch failure and never change output. */
bool d58_take_supervisor(struct d58_notice_queue *queue,
    const struct kernel_task_table *tasks,struct d58_notice *out,
    bool in_irq,bool live_if_disabled,bool supervisor_context);
#endif
