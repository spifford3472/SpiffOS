# ADR M10-D5.8c C2a — Genuine IRQ0 priority donation observation and safe cooperative waiter wake

Status: owner directed remaining D5.8c development on 2026-11-14; C2a is a separately flagged verification gate, NOT full IRQ-return donation/handoff, and no production authorization.

## Governing sources
AGENTS.md; accepted.md; open.md; architecture/system-overview.md; DEVELOPERS_GUIDE.md; ADR-m10-d53-bounded-synchronization.md; ADR-m10-d54-priority-dispatch.md; ADR-m10-d56-irq-handoff.md; ADR-m10-d57-preemption-and-priority.md; ADR-m10-d58b-frame-ownership-gate.md; ADR-m10-d58c-real-irq-lock-owner.md; kernel-sync-d53.c, kernel-dispatch-d54.c, m10-d54-guest-smoke.c, m10-d58c-irq-frame.c, x86-irq0.S. Exact-head CI run #85 completed success for C1.

## Design and irreducible context boundary
C1 validates real IRETQ preemption of an uninterrupted lock owner. D5.8b's high-priority BLOCKED waiter, however, owns a cooperative x86 RET stack, not a validated IRQ-return frame. M10-A cannot dispatch that task through IRQ0 without an independently built and authenticated execution bridge. A synthetic IRETQ frame or IRQ-time d53 mutator is forbidden. Nor may the blocked waiter restore IF while its old CPU stack still executes.

Stage C2a tests the *missing combined state* safely before attempting IRQ handoff. A test-only extension to the D5.4 real three-stack fixture runs a low-priority owner holding rank 0, a genuine high-priority task that enters D5.3 BLOCKED on its own CPU stack, and a separate medium-priority runnable CPU competitor. Boot dispatcher remains the independent surviving stack and is the sole cooperative M10-A dispatcher. While a genuine IRQ0 interrupts the low owner (which was selected over the medium competitor by the actual D5.4 priority adapter), a small read-only IRQ observer must validate the **physical** saved 20-qword source against D5.2b, D5.3 graph/ledger, D5.8b and C1 frame checks, confirm high waiter is BLOCKED and retained in the exact generation-linked D5.3 wait queue, observe live IF=0 and D5.3 effective TIME_SENSITIVE donation to the owner, and confirm medium is still a RUNNABLE competing task. Repeat at least three real interrupts; mutation count must remain zero. No M10-A task transition or lock mutation is permitted inside observer, and no extra timer EOI. Owner then releases from its own valid non-IRQ stack under IF=0, causing M10-A wake and ownership transfer; the high waiter resumes only through the genuine, previously saved **cooperative** continuation, releases its lock, and exits; off-stack supervisor cleanup and editor restoration follow.

This is *not* a real IRQ priority scheduler selection/handoff of owner-versus-competitor: the D5.4 policy chooses the donated owner before execution, real IRQ0 provides authenticated live donation observation, and normal CPU execution is still cooperative. The stronger full C2b requirement is a real M10-A IRQ-exit handoff during contention plus safe unification of the two context types, separately reviewable after C2a. Do not conflate these accomplishments.

## Acceptance and failure containment
- Test flag changes only D5.4 guest and x86 IRQ observer; no default/old test semantic drift, no new M10-A lifecycle owner.
- QEMU 128/256/512 MiB, true high BLOCKED generation+owned stack+IF-off transfer, low owner selection while medium READY, >=3 actual IRQ0 frames bound to low and D5.3 graph, lock transfer/wake high, original saved continuation only, independent off-stack cleanup, supervisor progress and EDIT restored.
- Invalid IF/guard, graph, orphan, frame, generation or return-context provenance must reject before unsafe dispatch; hosted ASan/UBSan and separate QEMU negatives. Full inherited acceptance, byte-identical unflagged ELF to D4 and independent exact-head CI.
- In-flight malformed source frames cannot safely be recovered by fake IRQ return or releasing live stacks; fail closed without claiming recovery. No owner ISO replacement, merge, physical disk, network or editor preemption.

Alternatives rejected: fabricating a READY waiter IRQ frame; treating read-only IRQ observation as IRQ scheduler handoff; moving D5.3 acquire/release into IRQ; busy-waiting with interrupts enabled after BLOCKED on the same stack. Tradeoff is an explicit intermediate proof gate rather than shipping unverified mode transitions.

2026-11-14: docs-first isolated C2a decision; C2b true IRQ task-handoff remains open.
