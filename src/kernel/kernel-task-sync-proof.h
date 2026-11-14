/* Purpose: D5.2c registered one-owner/one-waiter synchronization proof.
 * Responsibilities: couple lock ownership and waiter identities to D5.2b
 * reference ledger so retirement cannot discard live dependencies.
 * Dependencies: kernel-task-resources.h, authoritative M10-A task handles.
 * Invariants: one owner, at most one waiter, full-generation identities;
 * transfer adjusts both reference classes under one IF=0 critical section.
 * Concurrency: boot CPU only, caller owns IF=0, never from IRQ context.
 * References: ADR-m10-d5-supervisor-cleanup.md and AGENTS.md. */
#ifndef SPIFFOS_KERNEL_TASK_SYNC_PROOF_H
#define SPIFFOS_KERNEL_TASK_SYNC_PROOF_H
#include "kernel-task-resources.h"

enum kernel_sync_proof_result {
    KERNEL_SYNC_OK,
    KERNEL_SYNC_WAITING,
    KERNEL_SYNC_REJECTED
};

struct kernel_sync_proof_lock {
    bool initialized;
    bool owned;
    bool waiting;
    struct kernel_task_handle owner;
    struct kernel_task_handle waiter;
};

/* Initialize an unused zero-valued lock. Does not allocate a mutex. */
bool kernel_sync_proof_init(struct kernel_sync_proof_lock *lock);

/* The first registered task acquires one lock reference; at most one
 * second task can register as waiter. This policy-only fixture does NOT
 * block or wake the CPU: a later integration must join task state changes. */
enum kernel_sync_proof_result kernel_sync_proof_acquire(
    struct kernel_sync_proof_lock *lock,
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle caller);

/* Only exact owner may release. If there is a waiter, transfer lock owner
 * and remove wait registration atomically under caller's IF=0 exclusion.
 * Fail without mutation for stale/terminated/invalid participants. */
enum kernel_sync_proof_result kernel_sync_proof_release(
    struct kernel_sync_proof_lock *lock,
    struct kernel_task_resources *resources,
    const struct kernel_task_table *table,
    struct kernel_task_handle caller);
#endif
