# ADR M10-D5.2a — Execution-aware stack reclamation boundary

Status: OWNER-AUTHORIZED ISOLATED IMPLEMENTATION; no merge, normal-kernel preemption or accepted ISO deployment. Stacked on PR #71 D5.1.

Governing references: `AGENTS.md`, `docs/decisions/{accepted,open}.md`, `docs/architecture/system-overview.md`, `docs/decisions/ADR-m10-task-scheduler-foundation.md`, `docs/decisions/ADR-m10-d4-priority-supervisor-proof.md`, `docs/decisions/ADR-m10-d5-production-task-model.md`, `docs/DEVELOPERS_GUIDE.md`.

## Problem and decision

D5.1 makes task descriptors unavailable until the caller asserts cleanup through `kernel_task_reap()`, but it does not verify execution context or stack lifetime. The M10-B diagnostic already switches terminated tasks to the permanent boot context and releases its owner-backed 16 KiB stack frames on that surviving stack. D5.2a must connect these two operations without another scheduler.

**Implement a separate, bounded reaper adapter**, invoked only on the verified boot/dispatcher stack after returning from `x86_task_switch`. It verifies the exact generation-tagged TERMINATED handle, the retired task's registered stack range and ownership, the caller's actual current stack pointer is outside the retired stack, no task is currently RUNNING on that slot, and the registered saved RSP lies within the retired stack. It then invokes a restricted owner-provided release operation, marks descriptor reclaimed only after release success, and rejects repeated, active-stack, stale-handle and release-failure requests without marking descriptors reusable. The owner must have already eliminated outstanding waits, locks and other references; production synchronization will add separate checks before activation.

The interface deliberately has **no arbitrary user-facing callback or process ABI**. It accepts a single stack-owner token and a typed release function supplied by the kernel's memory owner. No allocation, logging, hardware I/O, or IRQ execution inside the pure decision/commit path. The x86 wrapper captures the real RSP in kernel code rather than trusting a task-supplied value. Caller owns single-CPU exclusion (IF=0) throughout the check/release/reap transaction. Initial integration is diagnostic-only in M10-B, not the regular editor.

### Failure atomicity caveat

A failed release must leave the descriptor unreaped. If release has succeeded but marking the descriptor reclaimed unexpectedly fails, the descriptor remains unreusable, and recovery requires supervisor investigation; NEVER return a reusable descriptor with live stack backing. This adapter must check table state and callback validity before releasing. A future production release adapter must have well-defined all-or-nothing semantics. The current M10-B test memory owner releases individual kernel frames and may encounter an unexpected partial-release failure. On that path it poisons its allocator, reports failure, and the reaper refuses to recycle the descriptor; this is fail-closed containment, **not a claim of transactional rollback**. Production task reuse remains gated until an independently verified recovery architecture can handle this case.

### Tests and rollout

1. Hosted sanitizer tests: valid off-stack reclamation, active-stack and malformed-range negatives, wrong generation and state, refused release, repeated release, reserved supervisor invariants, and iteration stress.
2. M10-B real QEMU integration: after cooperative task exits and returns to bootstrap stack, pass the actual RSP, verified saved RSP, allocated backing bounds and exact memory owner to reaper; test at 128/256/512 MiB. Preserve canary and rollback checks.
3. Run `make HOST_CC=gcc test-m10-d5-acceptance` including full D4 regressions; default kernel ELF isolation must remain identical.
4. Green exact-head CI and explicit owner approval precede merge/deployment. No physical media or OPNsense/network changes.

Alternatives rejected: reclaim inside task exit (use-after-free), trusting a boolean "switched" flag (unverifiable), embedding memory ownership inside D4 diagnostic controller (second scheduler), enabling general kernel IRQ preemption now (unsafe reentrancy). Future production task teardown requires wait/lock ownership retirement and a nonblocking dispatcher reaper queue.

## Change history

2026-11-13: owner explicitly elected next execution-aware cleanup checkpoint following D5.1. Documentation-first stacked isolated branch.
