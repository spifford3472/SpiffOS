# ADR M10-D5.2c — Bounded supervisor cleanup queue and registered synchronization proof

Status: OWNER-DIRECTED, ISOLATED IMPLEMENTATION ONLY. Stacked on PR #73 (D5.2b), #72 (D5.2a), and #71 (D5.1). No production preemption, merge, accepted ISO deployment, physical disk, or network changes authorized.

Governing references: AGENTS.md, docs/decisions/accepted.md, docs/decisions/open.md, docs/architecture/system-overview.md, ADR-m10-task-scheduler-foundation.md, ADR-m10-d4-priority-supervisor-proof.md, ADR-m10-d5-production-task-model.md, ADR-m10-d5-execution-aware-reaper.md, ADR-m10-d5-resource-ownership.md, docs/DEVELOPERS_GUIDE.md.

## Context

D5.2b registers stacks and tracks lock/wait counts, but those counters have no production mutex consumers, and final retirement is called directly by the diagnostic. D4's one-waiter priority-inheritance controller is **test-only** and owns a different state table. Joining its numeric worker slots to D5 task handles would create ambiguous ownership and unsafe cleanup.

## Decision and alternatives

Add one **statically bounded FIFO queue** for cleanup requests, carrying complete generation-tagged M10 handles. Enqueue only exact TERMINATED, registered, non-quarantined tasks. Reject duplicate entries, queue exhaustion, missing registration, stale generations and invalid table invariants without mutation. Queue processing is called **only by external dispatcher or reserved supervisor on a surviving stack with IF=0**. A task's outstanding resource references block processing; do not discard or silently clear them. Use a deterministic bounded scan of the current queue: blocked entries rotate to tail at most once per service call; eligible entries invoke the existing D5.2b registered-owner retire. A quarantined failed release is removed from the processing queue (but retained in the owner registry), with a stable queryable failure record. Queue state is never mutated from IRQ context. No allocations or background worker required.

Define a separate **small registered synchronization proof** using the authoritative M10-A task handles and D5.2b ref API. Allow exactly one owner and one waiter; all count changes and lock state updates must be explicitly preflighted and serialized, and only a trusted dispatcher can release ownership on behalf of a terminated task once independently validated. Such recovery is not a generic mutex ABI; do not add a path that simply zeroes outstanding counters after termination. The proof must reject duplicate owner/waiter, stale IDs, owner termination with live waiter, missed wakeup and arbitrary lock graph claims.

**Fail-closed recovery policy:** Existing stack-owner release may partially fail and poison the frame allocator. No retry and no automatic requeue. Quarantined records remain reserved until future owner-approved forensic reset/reboot; safe recovery of partially freed live backing has NOT been established. A supervisor may inspect status and report it, but must not revive or release uncertain backing.

Rejected alternatives: unbounded linked-list work queue (allocation and failure complexity), auto-freeing mutex ownership on task death (may violate integrity), merging D4 diagnostic scheduler into M10-A (parallel manager), unconditional retries of rejected frame releases (double-free risk), interrupts performing cleanup (incorrect stack and allocator context).

## Acceptance

Hosted GCC ASan/UBSan: queue wrap/fairness, 16 slots including supervisor reservation, duplicate/stale handles, full-queue rejection, blocked rotation, lock/wait registered consistency, successful off-stack retirement, refused release -> quarantine exactly once, no retry, and 20,000 cycle reuse stress. Real M10-B QEMU: use bounded supervisor queue for actual post-switch cleanup at 128/256/512 MiB, preserve active-stack rejection, canaries, exact owner and editor. Inherit full D5.2b/D4/M9 acceptance, compare ordinary normal ELF hash, check exact-head CI. Draft PR and owner review before any merge/deployment.

## Open production boundary

No general kernel mutex/wait manager is currently integrated. D4 single-waiter priority inheritance remains a separate diagnostic. No production task creation or IRQ switching is enabled here; lock graph, cancellation semantics, priority inheritance, legitimate supervisor recovery and all-or-nothing physical frame cleanup require separate review.

## Change history

2026-11-13: owner explicitly elected next checkpoint. Documentation-first stacked architecture record; validation to be added after actual tests.


## Implementation and validation record — 2026-11-13

Implemented in isolated branch `feat/m10-d5-supervisor-cleanup-queue`, draft PR #74, stacked on D5.2b PR #73 and earlier PRs. `kernel-task-cleanup.[ch]` adds a fixed FIFO carrying exact `(slot,generation)` identifiers. Every queued identity is checked before any release callback. The reserved supervisor slot 15 is never enqueued; a blocked ordinary task rotates at most once per bounded service visit, while eligible tasks are released by the D5.2b registered owner on the surviving boot stack. A release callback failure removes only the retryable work item, retains a quarantined registry binding, and forbids automatic retry. This module has **no IRQ entrypoint or ordinary editor linkage**.

`kernel-task-sync-proof.[ch]` is a hosted-only one-owner/one-waiter ledger client. It uses D5.2b reference updates for lock ownership, waiter registration and ownership transfer and rejects terminated owners. It is **not** the D4 priority-inheritance controller, does not block/wake actual tasks, and does not confer production mutex safety. Any owner with outstanding references remains unreclaimable until a separately verified recovery protocol resolves them. The existing memory allocator's partial failure remains quarantined, not transactionally rolled back.

The Forge QWEN local coding agent was used first for a tiny handle-equality helper, completed in 46.49 seconds. Coordinator reviewed and integrated equivalent full-generation comparisons and handled queue/synchronization implementations and tests. Codex not used. The first real QEMU build exposed a compiler-generated `memset` call from whole-structure FIFO initialization, unavailable in the freestanding kernel link; replaced by explicit scalar/array initialization. Added a full 15-ordinary-task capacity case; its first run exposed a **test-order-only** assumption about a global release counter; fixed to assert relative increments and reran both sanitizer gates.

### Actual isolated Forge tests

Disposable checkout: `/tmp/spiffos-d52c-20261113`; the owner checkout, accepted ISO and rollback files were not modified. Process-only Git safe.directory allowed read-only inspection of pinned Limine. The QEMU command used no network and isolated OVMF variables, and touched no physical disk.

- `make HOST_CC=gcc test-m10-d5-supervisor-queue`: **PASS** with GCC ASan/UBSan, marker `SPIFFOS_M10_D5_SUPERVISOR_QUEUE_OK`, 20,000 queued retirement cycles and full ordinary-slot, blocked, stale, ordinary-task, active-stack, quarantine and no-retry cases.
- `make HOST_CC=gcc test-m10-d5-sync-ledger`: **PASS**, marker `SPIFFOS_M10_D5_SYNC_LEDGER_OK`, 20,000 registered transfer/retirement cycles, contested owner/waiter, terminal owner and invalid caller negatives.
- `make HOST_CC=gcc test-m10-b-guest`: **PASS** in real QEMU at 128, 256 and 512 MiB; task stack released by bounded dispatcher queue after returning to permanent boot stack.
- Full `make HOST_CC=gcc test-m10-d5-supervisor-acceptance`: **exit 0**, runtime **263.07 seconds**, log `/tmp/spiffos-d52c-acceptance.log`. Markers `SPIFFOS_M10_D5_2C_ACCEPTANCE_OK`, `SPIFFOS_M10_D5_2B_ACCEPTANCE_OK`, `SPIFFOS_M10_D5_2A_ACCEPTANCE_OK`, `SPIFFOS_M10_D5_1_ACCEPTANCE_OK`, `SPIFFOS_M10_D4_COMPLETE_ACCEPTANCE_OK`, `SPIFFOS_M10_D4_CHECKPOINT_OK`, `SPIFFOS_M10_D3_ACCEPTANCE_OK`, `SPIFFOS_M10_D2_ACCEPTANCE_OK` all present. Existing keyboard/editor/F1–F12, PIT0/IRQ0, fault handling, memory, APIC/MMIO positive and negative regressions passed.
- Ordinary unflagged kernel ELF after stripping debug information: SHA-256 `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`, matching prior D4 normal executable baseline. The disposable normal ISO hashed `fafb6028c669b4f8deae886fe7c954f05387798694a1ce5d1398eebeef1271f8`; this is a disposable artifact only, not evidence of bit-for-bit owner ISO equivalence.

The exact final PR-head GitHub Actions result remains a separate gate. No source merge, production ISO deployment, USB/physical test or generalized preemption authorized. Future work requires a real authoritative synchronization implementation with task wake/block transitions, reference cancellation and scheduler integration before production lifecycle can be enabled.
