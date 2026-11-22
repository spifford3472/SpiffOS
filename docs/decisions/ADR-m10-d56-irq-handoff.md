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


## Implemented test-only boundary and verified Forge evidence (2026-11-14)

**Branch and review:** `feat/m10-d56-irq-authoritative-handoff`; draft PR #81 stacked on #80 through #71. The branch implements a privileged exact-`(slot,generation)` `kernel_task_irq_handoff` *within* M10-A. It prevalidates the current RUNNING generation and chosen RUNNABLE generation, membership and table invariants before a single IF=0 mutation; failed calls preserve task table and caller output. No D4 task controller is involved in the new guest. The original cooperative APIs remain prohibited in IRQ.

`SPIFFOS_M10_D56_IRQ_TEST` links a **separate** QEMU-only bridge to the original M9 x86-irq0.S entry and IRETQ return, after one normal timer callback and LAPIC EOI. It checks authentic source CPU frame/slot generation using D5.5c for worker frames; prevalidates an independently owned saved target frame, RSP, CS/SS, canonical RIP/RSP, IF, no owned mutex/wait refs and zero preempt-critical depth before invoking M10-A's exact handoff. Two **non-yielding** workers and the M10-A reserved supervisor execute on three independent 16KiB M2-owned physical stacks. Supervisor IRQ selector activates only after first-entry actually enters its owned stack. Every worker runs nested IF=0 critical regions while the timer is masked. A trusted boot dispatcher reclaims stacks only after supervisor leaves its execution stack. No scheduler resource allocation, serial/log, EOI or mutex mutation runs inside IRQ.

**QWEN-first workflow:** Forge local `qwen3-coder:30b` was given a narrowly scoped ready queue/exact-generation helper after coordinator inspected AGENTS and governing docs. QWEN returned an incorrect reference to nonexistent table-wide `generation`; the coordinator rejected that unintegrated snippet, authored the handoff against the actual per-task M10-A generation contract and tested it. Codex was not used.

### Verified test ledger

- `make HOST_CC=gcc test-m10-d56-handoff`: **PASS** hosted GCC `-Werror`, AddressSanitizer/UBSan, `SPIFFOS_M10_D56_IRQ_HANDOFF_OK`. Exact generation, immutable invalid states/outputs, reserved supervisor and **60,000** successive M10-A IRQ handoffs verified.
- `make HOST_CC=gcc test-m10-d56-guest`: **PASS in 33.37 seconds** from isolated Forge tree; real QEMU **128, 256, 512 MiB** each switched two independently owned, non-yielding M10-A workers for **40 IRQ slices each**. Verified actual on-stack supervisor completed work, maximum **3 unserviced hardware IRQ ticks** at 100 Hz, bounded nested critical sections, off-stack retirement, intact editor and normal IRQ0. Three negative builds—invalid saved target frame, forged active critical depth, stale destination task generation—each rejected before unsafe IRETQ/keyboard at all three sizes (**9/9**). Stale destination request explicitly verifies `current_slot` and `ready_count` did not change on rejection. Real saved source frame is not modified in these negative tests.
- Full `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine make HOST_CC=gcc test-m10-d56-acceptance`: **exit 0 in 357.76 seconds**; isolated path `/tmp/spiffos-d56-20261114`, log `/tmp/spiffos-d56-acceptance.log`, marker `SPIFFOS_M10_D56_ACCEPTANCE_OK` and all prior D5.5c/b, D5.5, D5.4, D5.3, D5.2d/c/b/a, D5.1, D4-complete/checkpoint, D3 and D2 PASS. Includes M9 keyboard/editor/workspace, real QEMU IRQ0 clock/disabled, x86 #UD/#DF faults, D4 stack-guard/invalid-frame negatives, APIC/MMIO cache/alias/rollback positives and negatives.
- Normal unflagged debug-stripped kernel ELF SHA-256 `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`, **identical to established D4 normal executable baseline**. Disposable isolated ISO SHA-256 `210f28c6bb42a2498225a94f74bf27ae07d480f14c345c1b6218d1ce370c7219`; no accepted ISO overwrite or release ISO byte-equivalence claim. Owner's Forge checkout `git status --short` clean. QEMU `-net none`, isolated transient OVMF variables, no physical disks or network changes.
- Final exact-head GitHub Actions is a **separate** gate and needs explicit verification before marking review-ready.

### Limits / remaining production gates

The D5.6 fixture is real preemptive M10-A IRQ-exit CPU scheduling, but **not a production-capable generalized kernel preemption subsystem**: it has two controlled workers, one supervisor, bounded FIFO rotation and test-specific nested IF masking. D5.3 priority inheritance in the real IRQ selector, preemption-disable coverage for all production locks/drivers/allocators, robust source-frame corruption recovery, authoritative saved frame refresh and controlled owner-death cleanup, FPU/SIMD state, CR3/Ring3, multicore, physical CPU-time supervisor service and public user task ABI remain OPEN. Do not enable normal editor preemption, merge stacked drafts or deploy any physical boot artifact without subsequent owner approval.
