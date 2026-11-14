# ADR M10-D5.6 — Authoritative IRQ-exit handoff, transactional state, and safety bounds

Status: owner selected isolated M10-D5.6 development on 2026-11-14. NO production editor preemption, merge or ISO deployment. References: AGENTS.md, accepted.md, open.md, system-overview.md; ADR-m10-d55c-task-frame-budget.md; ADR-m10-d55-preemption-safety.md; ADR-m10-d54-priority-dispatch.md; ADR-m10-d53-bounded-synchronization.md; ADR-m10-task-scheduler-foundation.md; x86-irq0.S/M10-D2 frame ABI.

## Context and constraint
The M10-A task API explicitly prohibits IRQ calls, even with IF=0. D4's separate diagnostic controller proves real IRQ-exit switches but must not become a second production task authority. D5.5c now validates real saved frames on an exact-generation M10-A worker. The next test must prove multiple non-yielding workers can be switched via **M10-A as sole lifecycle owner** without importing D4's task state.

## Decision
- Introduce an explicitly privileged **M10-A exact-handle IRQ handoff** function, called exclusively by the trusted IRQ0 test-only bridge. It is the sole RUNNING→RUNNABLE and selected RUNNABLE→RUNNING mutation, with full table validation, exact nonzero source/destination generations, queue membership, finite capacity, no overlap and no mutation on rejected requests. A no-op return for same handle is explicit. Do not call legacy cooperative APIs from IRQ or reuse D4 lifecycle.
- Bridge must preflight the real current 20-qword frame using D5.5c exact stack+generation ownership; preflight next saved frame within independently registered backing, including canonical RIP/RSP, RFLAGS/segments and critical-depth/lock guards, before ANY scheduler change. For supervisor, frame is on surviving boot stack (no D5.2b worker binding); exclusive privileged identity is reserved M10-A slot 15.
- Only after both frames pass, the bridge invokes M10-A's atomic IRQ handoff and returns its exact destination frame pointer to original x86-irq0.S IRETQ. Never call allocator, logger, mutex mutation, reaper or EOI from IRQ. No IRQ nesting; saved interrupt flags reinstate IF only through IRETQ. Preserve all GPRs, no FPU/SIMD.
- Define a small **preempt-disable depth counter** on the diagnostic bridge only. Test lock-like critical sections mask IRQ0 and bracket with nested counter. A tick arriving during an active critical region must defer switching, never run a task mutator; the bridge tests deliberate rejection of forged nonzero depth before mutation. Owner-death and stale frames fail closed and return to known supervisor frame if safe; unknown trusted fallback must halt diagnostically, not guess.
- The boot supervisor runs real service completions on its permanent stack and a hardware-tick monitor enforces a bounded max tick gap. Worker loops do not yield. A fail-closed missed deadline must not be auto-cleared by dispatch selection. Initial fixture uses FIFO-like bounded supervisor reservation from existing D1 policy, not new priority state. Do not conflate tick-count guarantee with physical hard realtime.
- Under `SPIFFOS_M10_D56_IRQ_TEST` **only**, add 2 independently M2-owned non-yielding workers and the reserved M10-A supervisor, genuine frame switches, negative forged-generation and invalid-next-frame tests, 128/256/512MiB QEMU and editor return after off-stack release. Keep normal ELF byte-identical.

## Alternatives rejected
No D4 second controller, no calling cooperative M10-A APIs in IRQ, no unsafe separate task queue, no unvalidated pointer return, no automatic owner release. No user program activation, Ring3, FPU/SIMD or multicore support.

## Acceptance
Docs/ADR before edits, local QWEN first for narrow helper, hosted ASan/UBSan exact handoff tests, real QEMU all RAM sizes positive + negative, supervisor tick evidence, full inherited M9/M10 acceptance, default ELF SHA baseline, exact-head CI. Keep draft PR, no merge/deployment.

## Open
Exact production priority donation from D5.3 in IRQ context, preemption-disable integration into all kernel locks/drivers, genuine hardware interrupt-stack isolation, exception/task death, SIMD/FPU context save, CR3/Ring3, physical CPU-time deadlines, firmware and multi-CPU support.

## History
2026-11-14: D5.6 authorized as isolated milestone, no release authorization.
