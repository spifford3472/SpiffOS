# M10-D4 completion — four-class preemption acceptance ledger

Status: **full D4 diagnostic scheduler implementation under regression testing**.
Owner authorized completion of D4 on 2026-11-13. This is *not*
authorization to enable preemption in the ordinary editor or physical
hardware. The user-space ABI, ring 3 isolation, FPU/SIMD, CR3 switching,
SMP and generalized production scheduling remain M10-D5 work.

## Required governance

Read/observed `AGENTS.md`,
`docs/decisions/accepted.md`, `docs/decisions/open.md`,
`docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-task-scheduler-foundation.md`,
`docs/decisions/ADR-m10-d-preemption-proposal.md`,
`docs/decisions/ADR-m10-d4-priority-supervisor-proof.md`,
and `docs/DEVELOPERS_GUIDE.md`.

## Architectural implementation

- `src/kernel/m10-d4-task-control.[ch]`: bounded single-CPU READY,
  BLOCKED and QUARANTINED states with protected worker self-blocking,
  supervisor-only wake/quarantine/recovery, immutable ready snapshots,
  saturation-based per-worker aging and strictly reserved supervisor
  slot 15. Four static worker identities map Background, Normal,
  Interactive and Time-sensitive exactly. A single-waiter lock implements
  priority inheritance during contention; a blocked high-priority waiter
  donates effective priority to the low-priority owner. Release restores
  the owner's base priority and wakes/transfers ownership to the exact
  waiter. Nested locks, deadlock cycles, multiple waiters and multi-CPU
  contention remain out of scope.
- `src/kernel/m10-d4-complete-guest.c`: FOUR independent 16 KiB
  frame-backed ring-0 worker stacks. The normal M9 IRQ0 first saves the
  complete 20-qword hardware/software frame, accounts one tick and emits
  one LAPIC EOI; ONLY then the gated D4 completion IRQ-exit selector
  chooses an owned saved frame and IRETQ resumes it. Worker0 actually
  blocks its own execution and is woken by the surviving supervisor.
  Worker3 actually blocks behind Worker0's lock and donates priority.
  Supervisor quarantines an inactive worker, corrupts its saved CS,
  rejects attempted recovery, restores/validates the existing trusted
  full frame and permits it to resume.
- Measured supervisor reservation is every fourth **selection** while
  ready; QEMU validates maximum supervisor IRQ gap <=4 ticks (~40ms at
  approximately 100Hz). This is scheduling observation, **not**
  deterministic physical CPU time or a hard realtime guarantee.
  No locks, allocation, frame cleanup, serial or rendering occur in IRQ.
  Only after IRETQ returns to the original boot stack does the
  dispatcher check all canaries, release all 16 worker pages and
  publish audit metrics.
- `SPIFFOS_M10_D4_COMPLETE_TEST` is a new *separate* diagnostic
  flag. The ordinary kernel and the earlier D4/D3 flags are unchanged.
  `src/kernel/memory-boot.c` extends the existing audited mapped
  four-page owner provider from two to four worker slots ONLY for this
  diagnostic, preserving per-frame rollback and exact owner checks.

## Verified targeted tests

At exact source SHA
`bda20abc5fa6d781c74d061d25b85efc87d36b69` on isolated Forge:
`make HOST_CC=gcc test-m10-d4-state test-m10-d4-complete-guest`
passed. ASan/UBSan state test marker
`SPIFFOS_M10_D4_TASK_CONTROL_OK`; real QEMU 128/256/512 MiB
full four-worker passes plus negative disabled-IRQ, bad saved-frame and
bad stack-canary tests (12 guest cases total), no exceptions or invalid
keyboard entry. In each positive guest, exact serial metrics:
Background 12 slices; Normal 15; Interactive 27; Time-sensitive 165;
supervisor 73 dispatches; worst supervisor gap **3 ticks**;
elapsed timer time **2930 ms**. All four workers progressed and
preserved/retired owned stacks. Lock contention and supervisor
frame quarantine/recovery succeeded.

The diagnostic displays true metrics via serial only on the
surviving boot stack after completion. It does not assert a CPU
percentage or globally guaranteed real-time execution service.

The independent **ordinary unflagged** ISO builds of prior D4 and
full D4 source both produced identical debug-stripped kernel ELF
SHA-256:
`82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`
(whole executable byte-equivalent). No D4 completion code is linked
into normal SpiffOS. No host-owned ISO or source checkout was changed.

## Failure/negative cases

1. Real IRQ0 unavailable: fail closed before any task handoff.
2. Corrupt saved CS of inactive worker3: IRQ selector refuses to
   dispatch the frame and returns to safe surviving supervisor;
   no fatal CPU exception or false success.
3. Corrupt low-stack canary after all workers have stopped: reject
   completion, release all allocated backing, do not enter keyboard.
4. Corrupt saved CS of quarantined worker2 during supervisor recovery:
   reject unsafe recovery; only a restored and revalidated owned
   frame can resume.

These are controlled *task-state* failure cases, not x86 #PF/#UD
general exception recovery. Fatal processor exceptions remain fatal.

## Agent review and quality

Local QWEN `qwen3-coder:30b` was given a narrow C11 state-controller
coding task with governing documents. It generated an unconstrained,
unvalidated raw task-ID sketch with fake supervisor authority and a
nonexistent frame validation gate, rejected by the coordinator rather
than merged. The coordinator implemented strict fixed-size state
controls and sanitizer/real-QEMU tests. A second narrowly scoped QWEN
audit suggested five concerns; independent source inspection found
those were already handled: fixed worker ID equals immutable base
priority, lock waiter reset occurs on transfer, owner priority resets
on release, worker READY is checked before block, and self-quarantine
is rejected. No Codex fallback was necessary.

## Rollback and remaining approval

Work is isolated on `feat/m10-d4-completion-20261113` above the
draft D4 #69 and D3.1 #68 PR branches. Neither branch is merged or
deployed yet. Current owner ISO remains the accepted M10-D3
SHA-256 `4fcda82804c72fb4697175b811865755deb3caa4605797bce7a1d7c521eda596`
with the previous rollback retained. Discarding this review branch
or its temporary QEMU tests completely rolls back D4 experiments.
Do not install a D4 diagnostic or ordinary ISO without a verified
owner-controlled merge and release approval.
