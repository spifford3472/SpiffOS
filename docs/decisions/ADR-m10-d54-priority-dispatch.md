# ADR M10-D5.4 — Authoritative effective-priority dispatch and QEMU multiwaiter proof

Status: owner-directed **isolated implementation** (2026-11-13), stacked on draft PR #76 and previous D5 branches. Governing: AGENTS.md; accepted/open decisions; architecture overview; ADR-m10-task-scheduler-foundation; ADR-m10-d4-priority-supervisor-proof; ADR-m10-d5-production-task-model; ADR-m10-d53-bounded-synchronization; DEVELOPERS_GUIDE.

## Context
D5.3 computes transitive priority donation but M10-A always consumes the FIFO head. D4's diagnostic priority controller must remain separate. D5.2d's two-worker QEMU test does not prove D5.3's *multiwaiter* lock execution. We must not introduce a second lifecycle state owner.

## Decision
Add an **exact-handle selected dispatch operation inside the existing M10-A table**. It applies the same single RUNNING transition, removes exactly one known RUNNABLE entry, validates the whole table and full handle/generation before any mutation. Existing FIFO API remains unchanged for existing tests. A separate bounded *selection adapter* builds candidates solely from M10-A's ready queue and the D5.3 generation-tagged priority/lock ledger; it computes effective priorities, waits/age and supervisor reservation, then invokes the M10-A selected dispatch. Neither adapter duplicates task states, context ownership, locking or stacks.

The supervisor has exclusive slot 15, never enters D5.3 ordinary lock graph, is always selected at least every fourth dispatch **when RUNNABLE**. If not runnable, there is no invented supervisor thread or forced dispatch. Selection is single boot CPU, IF=0, non-IRQ, no allocation. Keep deterministic starvation mitigation through bounded ready-age score; no guarantee of CPU-time realtime deadlines or IRQ preemptive scheduling. Reject missing/stale generation profiles and inconsistent registries without changing task state, age or output. Priority donation is recomputed from the registered lock graph so it disappears on unlock/cancellation.

A separately flagged M10-B test-only QEMU cooperative fixture must demonstrate >=3 independently stacked workers: owner and two blocked waiters, donor ordering, transferred ownership/wakeup, resuming the *actual* saved C continuation after switch-away, and safe final memory release from surviving dispatcher. Preserve established two-worker regression and normal editor binary.

## Safety acceptance
Host GCC ASan/UBSan: selected-dispatch identities, corruption and no mutation, supervisor reserve under sustained load, donated priority selection and revocation, tie fairness, starvation bounds and 20,000 generation reuse. Real QEMU 128/256/512 MiB multiwaiter worker switch tests plus inherited M10/D4/M9 full acceptance and negative cases; default ELF hash comparison. Keep isolated PR draft and exact-head CI; do not merge, deploy accepted ISO, touch physical drives, enable normal editor preemption, Ring 3 or network. D5.3 orphan locks still fail closed.

## Open
Supervisory service measured in *dispatches*, not hard CPU time; IRQ-preemptive lock safety, actual kernel user task ABI, stack/interrupt race handling, nested lock recovery and owner-death protocol require later approval.

## History
2026-11-13: owner selected D5.4 following D5.3; documentation-first scope approved for isolated implementation, not release.
