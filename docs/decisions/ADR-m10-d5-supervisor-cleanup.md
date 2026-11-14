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
