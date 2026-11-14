# ADR M10-D5.2d — Synchronized block/wake on authoritative task model

Status: OWNER-AUTHORIZED ISOLATED IMPLEMENTATION, not approval to enable production preemption. Stacked on D5.2c PR #74, D5.2b PR #73, D5.2a PR #72 and D5.1 PR #71. Read AGENTS.md, accepted/open decisions, architecture overview, ADR-m10-task-scheduler-foundation.md, ADR-m10-d4-priority-supervisor-proof.md, ADR-m10-d5-production-task-model.md, ADR-m10-d5-resource-ownership.md, ADR-m10-d5-supervisor-cleanup.md and DEVELOPERS_GUIDE.

## Problem

D5.2c's one-owner/one-waiter synchronization proof changes ledger counts but never blocks a RUNNING waiter or wakes it. M10-A already has authoritative `kernel_task_block()` (RUNNING -> BLOCKED) and `kernel_task_wake()` (BLOCKED -> RUNNABLE), yet no atomic lock-to-scheduler integration. D4's priority inheritance controller is diagnostic-only and is not a production task manager.

## Decision

For this isolated checkpoint, add an **internal one-owner, one-waiter kernel mutex** backed only by M10-A handles, D5.2b resource ledger, and the preexisting M10-A block/wake transitions. Preserve the M10-A public ABI and its single CPU FIFO policy. A current RUNNING waiter trying to acquire a held mutex registers one waiter reference, blocks via `kernel_task_block`, and must yield its actual CPU context to the dispatcher before any other task runs. A current RUNNING owner releases, registers ownership for the blocked waiter, drops its waiter reference, wakes exactly that handle into the ready FIFO, and releases the former owner's registered ownership. No instant context switch or IRQ-entry actions. A woken task resumes from its saved context and sees the successful lock transfer. Waking must not occur if an owner cannot atomically complete bookkeeping; failure must leave all metadata and references unchanged.

The mutex cannot be recursively acquired, have more than one waiter, or have its own scheduler. References are generation tagged, lock state is bounded and caller-owned. Preflight the entire task table, resource registry, lock state, exact running owner/caller, counts and task states **before mutations**. All operations are one-CPU IF=0 critical sections; an interrupted or arbitrary task must not operate the lock. Unlock with a nonblocked or terminated waiter fails closed and retains original ownership. Only a dispatcher that actually saved the blocked task's context may next dispatch; the metadata transition is not proof of a real x86 context switch. Mutex destruction, dead owner cancellation, multiple locks/waiters, priority inheritance and general lock graph are deferred.

## Validation

- Hosted GCC ASan/UBSan: uncontended acquire/release, held-mutex contention blocks real M10-A RUNNING task, exact wake and transfer via FIFO, blocked waiter cannot dispatch, stale-generation and wrong owner denial, recursive and second-waiter denial, owner death and waiter death freeze without invented cleanup, duplicate wake rejection, no mutation on invalid release, 20,000 cycles of reuse, and preservation of supervisor reservation.
- Isolated QEMU proof: if adding a new execution guest integration, prove actual cooperative transfer to surviving boot dispatcher and preserved execution stack. Never treat metadata-only blocking as sufficient to run a second task concurrently.
- Run full inherited D5.2c/D4/M9 acceptance, default unflagged kernel hash comparison and final exact-head GitHub Actions.
- No merge, accepted ISO overwrite, production editor preemption, network/OPNsense or physical disk changes. Owner approval required for release.

## Rejected alternatives

Replacing M10-A with D4 controller duplicates lifecycle authority. Blocking via a helper that only increments a counter permits dispatching a supposedly waiting task. Automatically clearing lock owners when terminated can corrupt other tasks. Moving mutex operations into IRQ0 permits unsafe stack and allocator reentrancy.

## Change history

2026-11-13: owner selected D5.2d as next milestone. Documentation-first stacked branch. Test evidence recorded after execution.
