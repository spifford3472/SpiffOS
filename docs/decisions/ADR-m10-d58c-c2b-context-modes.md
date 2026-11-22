# ADR M10-D5.8c C2b Phase 0 — Exact-generation IRETQ versus RET context admission

Status: isolated design, hosted implementation and tests authorized as part of owner's 2026-11-14 continuation. NOT authorization to activate real IRQ switching among mixed context forms, merge, or deploy.

## Sources

AGENTS.md, docs/decisions/accepted.md, docs/decisions/open.md, docs/architecture/system-overview.md, docs/DEVELOPERS_GUIDE.md, ADR-m10-d53-bounded-synchronization.md, ADR-m10-d54-priority-dispatch.md, ADR-m10-d56-irq-handoff.md, ADR-m10-d57-preemption-and-priority.md, ADR-m10-d58b-frame-ownership-gate.md, ADR-m10-d58c-real-irq-lock-owner.md and ADR-m10-d58c-c2-blocked-irq-observation.md. C1 PR #85 exact-head CI success; C2a draft PR #86 has independent CI gate still required. No default kernel preemption approval.

## Context and decision

The true high-priority waiter in C2a is registered BLOCKED and is suspended on a six-callee-saved-register plus RET continuation. The D5.6 real IRQ0 scheduler, by contrast, resumes a verified 20-qword IRETQ saved frame. No API may infer that waking a waiter makes its previously saved RET stack an IRQ target; M10-A's READY state is necessary but not sufficient. Never invent a CPU RIP/RSP or recast the six-register RET stack in place as an IRETQ frame.

Introduce a read-only, caller-owned exact-generation context-form admission helper used initially only by hosted tests. For an IRQ return, a destination requires (1) a form explicitly marked VERIFIED_IRQ_IRETQ for that exact task generation, (2) M10-A exact RUNNABLE state and membership, (3) current source exact RUNNING identity with a hardware-supplied IRQ0 frame, (4) both independent C1 full frame proofs including D5.3/D5.2b graph, (5) live IF=0 and saved IF=1, (6) no active preempt guard, and (7) absence of unsafe orphaned/blocked context. The helper cannot establish hardware provenance from a caller-supplied boolean, nor perform a CPU switch; only x86_irq0.S can supply actual frame evidence to the future bridge. A cooperative RET waiter can be admitted to the original D5.4 off-IRQ dispatcher only after exact M10-A wake/transfer with an independently verified saved cooperative context; such a waiter is always rejected by the IRQ admission, even after becoming RUNNABLE.

The low-priority owner may enter an IRQ phase while running in a valid cooperative task context; its actual hardware IRQ source subsequently supplies a new verified full frame. This does not convert its parked cooperative continuation by reinterpretation. A medium CPU worker may instead begin from a separately initialized, independently verified IRETQ entry frame and be switched by M10-A only after full source/target proof. A reserved supervisor needs its own owned IRETQ frame and measurable completed work. After the real IRQ phase, the owner must disarm the IRQ selector under IF=0 before releasing rank 0 and waking the RET waiter; no post-release IRQ selector may see that waiter as an eligible IRETQ target. Boot/supervisor then resumes the original RET continuation through a separate, verified non-IRQ cooperative handoff. Merely changing a context-form enum never changes the physical CPU stack.

## Mandatory tests before any IRQ bridge

Hosted strict GCC ASan/UBSan must prove valid exact-generation source/target IRQ-frame pairs with real M10-A/D5.3 ledgers; reject RET-form READY target, RET-form BLOCKED target, stale form generation, invalid full frame, wrong IF/guard, graph corruption, orphan, supervisor mode confusion, and unchanged authoritative states/frames on refusal. Gate must be pure and bounded; not a second task scheduler or mutation queue. Separately QEMU must prove the real physical x86 context-form lifecycle and actual IRQ0 handoff between a donated owner and a medium worker, plus safe off-ramp and high waiter cooperative wake/release, three independently owned ordinary stacks and a reserved supervisor, fault injection, bounded real CPU service and editor restored. No claim of C2b completion on hosted gate alone.

## Tradeoffs and rollback

A two-phase cooperative/IRQ test bridge avoids fake frame synthesis but adds synchronization and mode transitions. The safest initial work is a hosted admission boundary, followed only later by flagged QEMU physical transitions. Alternatives rejected: letting kernel_task_wake imply IRQ-return eligibility, conversion with memcpy between unrelated frame layouts, calling D5.3 mutators in ISR, skipping complete frame/guard checks, killing blocked stacks, or using a second scheduler. Discard isolated worktree/branch for rollback; owner D4 ISO and physical disks remain untouched.

History: 2026-11-14 C2b Phase 0 docs-first architecture prior to any source changes.
