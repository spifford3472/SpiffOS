# ADR M10-D5.8 — Real IRQ mutex contention and deferred synchronization boundaries

Status: owner-directed isolated implementation and tests, 2026-11-14. Not production preemption, merge, deployment or new public ABI approval.

## Context and governing references

Read `AGENTS.md`, `docs/decisions/accepted.md`, `docs/decisions/open.md`, `docs/architecture/system-overview.md`, `docs/DEVELOPERS_GUIDE.md`, `ADR-m10-d53-bounded-synchronization.md`, `ADR-m10-d54-priority-dispatch.md`, `ADR-m10-d56-irq-handoff.md`, `ADR-m10-d57-preemption-and-priority.md`, and `docs/architecture/m10-d57-production-preemption-audit.md`.

D5.7 proves read-only donation-based IRQ selection separately from actual lock-contention execution. D5.3 acquire/release/cancel require a validated task/dispatcher context and IF=0; none may run in IRQ. Only M10-A owns task state. D5.7 already provides exact-generation IRQ guard and frame admission. D4 tested ISO remains the accepted fallback.

## Staged decision

1. D5.8a: implement a fixed-capacity, non-allocating, single-CPU *notification* mechanism for IRQ-exit scheduling observations, with trusted caller provenance, generation-tagged identity and bounded sequence. It is NOT a mutex transaction queue: notifications carry no lock mutations, pointers or ownership transfers. On invalid provenance, overflow, stale generation or corruption latch diagnostic failure and do not change the authoritative task table or D5.3. Prove hosted ASan/UBSan negative paths and bounded drain at a non-IRQ supervisor safe point.
2. D5.8b: in a separate QEMU diagnostic, run a low-priority lock owner, a higher-priority waiter and competing CPU worker, plus reserved supervisor. Execute D5.3 acquire/release/cancel only on verified current task or privileged safe-point stack under IF=0. After BLOCKED, switch away using the genuine M10-A task lifecycle; never resume a blocked stack prematurely. At IRQ exit use the existing read-only D5.7 selector and M10-A atomic handoff; prove dynamic donation, release, wake, and revocation across actual 100Hz interrupts, with immutable frame/stack ownership before IRETQ.
3. D5.8c: negative QEMU tests: corrupt graph, invalid destination or source saved frame, stale waiter/owner generation, guard active, orphaned owner and queue overflow; reject before mutation or unsafe IRETQ; do not force unlock or free quarantined memory. Supervisor work must actually complete within measured tick budgets, not merely be selected.
4. Full inherited D2–D5.7 and M9 regressions at 128/256/512MiB, normal debug-stripped ELF identical to D4, exact-head CI and source/ISO checksums. Every phase requires review and tests before proceeding.

## Trade-offs and exclusions

Deferred signals preserve IRQ boundedness but can overflow or become stale: fail closed, never silently discard and never mistake an observation for synchronization authority. Real locks remain owned by tasks and the authoritative registry. No blanket CLI instrumentation of kernel drivers; no normal editor/BASIC preemption, Ring3/CR3, FPU/SIMD, SMP, physical boot or infrastructure changes. A green diagnostic is not certification of production preemption.

## Validation and rollback

Develop in disposable Forge checkout only; do not replace `/home/geoff/projects/spiffos-qemu-main/build/spiffos.iso`. Compile new code only in explicitly selected diagnostic targets. Hosted tests first, then real QEMU, then inherited suite, independent exact-head GitHub CI. Roll back simply by discarding isolated feature branch; owner D4 ISO remains untouched. Record commands, results and failed attempts rather than assuming acceptance.

## Change history

2026-11-14: documented first staged D5.8 contract, gates, exclusions and rollback before implementation.

## D5.8a implementation checkpoint and necessary D5.8b architecture review

The isolated D5.8a queue and genuine IRQ0 supervisor-drain fixture are implemented and documented in `../operations/m10-d58a-deferred-irq-observations-20261114.md`. **Do not infer mutex implementation** from this diagnostic. In the current D5.6 IRQ bridge, `frame_safe()` rejects any frame whose D5.2b registration has `owned_locks` or `registered_waiters` nonzero. A live lock-owning worker thus deliberately fails IRQ admission. D5.8b must first define how a correctly validated D5.3 lock graph, generation-tagged task ledger and per-task saved frame can authorize a lock-holding worker, and separately how a waiter can become BLOCKED and switch to a survivor *without requiring an invalid IRQ return to its blocked stack*. This is a safety/ownership design gate, not permission to bypass existing checks.
