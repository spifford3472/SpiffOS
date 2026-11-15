# ADR M10-D5.8c — Real IRQ lock-owner frame admission before complete waiter handoff

Status: owner elected next isolated development/test step, 2026-11-14. Draft and diagnostic only; not general lock preemption or normal app activation.

## References and verified prerequisite

Required: AGENTS.md, docs/decisions/accepted.md, docs/decisions/open.md, docs/architecture/system-overview.md, docs/DEVELOPERS_GUIDE.md, ADR-m10-d53-bounded-synchronization.md, ADR-m10-d54-priority-dispatch.md, ADR-m10-d55c-task-frame-budget.md, ADR-m10-d56-irq-handoff.md, ADR-m10-d57-preemption-and-priority.md, ADR-m10-d58-irq-mutex-integration.md, ADR-m10-d58b-frame-ownership-gate.md, and m10-d57-production-preemption-audit.md. PR #84 exact-head commit 7d3cb42 passed GitHub Actions run #83. This is the baseline, not merge authorization.

## Problem and decision

D5.6 authentic x86 IRQ0 frames are 20 qwords, read from a generation-owned worker stack; it rejects every nonzero lock reference via both frame_safe() and d55c_frame_matches(). D5.8b's metadata-only graph admission now distinguishes legitimate lock owners and excludes blocked waiters, but cannot itself authenticate the CPU interrupt frame. The D5.8b cooperative BLOCKED continuation uses a different x86 saved-register context. An IRQ return into such a context is invalid.

Gate C1: add a **separate, diagnostic-only read-only lock-aware IRQ frame validator** that first checks the entire D5.3 graph and task+stack ledger using D5.8b, then independently proves actual frame location, canonical RIP/RSP, saved CS/SS/RFLAGS, saved user RSP above the frame, real IRQ interrupt provenance, IF=0, guard depth=0 and exact current RUNNING source or READY target generation. Reuse established 4-level paging and the D2 20-qword ABI. No state changes, frame writes, FPU/CR3 assumptions, allocations, EOI, blocked-task admission or speculative recovery. No change to old D5.5c/D5.6 validators in default builds.

C1 QEMU flags isolate the existing D5.6 IRQ-IRETQ workers and supervisor. One actual low-priority worker acquires D5.3 rank-0 lock at a valid non-IRQ IF=0 safe point on **its own running stack**, then CPU spins without yielding; IRQ0 must authenticate and preempt that worker while it still owns the lock. This uses the new frame validator only for proven lock-bearing ordinary workers, never for the reserved supervisor. The worker subsequently releases rank-0 from the same safe-point context, and the supervisor validates final counters. A second CPU worker and reserved supervisor continue to run; no claim of an actually BLOCKED waiter or dynamic donation. The D5.7 selector remains read-only, M10-A remains the only IRQ handoff mutator.

Gate C2 (separate review): combine an actual high-priority BLOCKED waiter and independent medium CPU worker with the IRQ-owned low-priority worker. Before adding a third active worker, prove how D5.8b's IF-off cooperative blocked-stack surrender rejoins an IRQ-IRETQ scheduler without corrupting the distinct saved context representations or assuming an owner is running when it is not. Do not synthesize frames or mutate M10-A from an IRQ. Only afterwards can real-IRQ dynamic priority donation/wakeup be claimed.

Gate C3: corrupt source/target frame, stale generation, graph/ledger, critical depth, orphaned lock, deadline, supervisor recovery, and no forced lock release. Full host/QEMU 128/256/512, inherited D2–D5.8b, exact-head GitHub CI, unchanged default ELF and owner ISO required.

## Alternatives, safety and rollback

Reject blanket deletion of nonzero lock checks, running D5.3 mutators in IRQ, forged task context while on a supervisor stack, or reinterpreting a cooperative RET layout as an IRETQ layout. Tradeoff: C1 only proves IRQ-preempting a legitimately locked worker, not priority inversion control or completed D5.8c.

Develop in disposable Forge worktree, QEMU -net none and no physical disks; no owner ISO change or merge. Failed frame/graph/provenance must refuse handoff and retain original state and owned locks, never auto-release. Discard the isolated branch to roll back.

## Change history

2026-11-14: recorded staged C1→C2→C3 architecture and hard stop before editing code.
