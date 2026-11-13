# ADR M10-D — proposed timer-driven preemption of kernel tasks

Status: **OWNER APPROVED FOR ARCHITECTURE AND IMPLEMENTATION**, 2026-11-13.
Merge, ISO deployment and physical hardware changes are NOT authorized.
Design and implementation proceed in isolated reviewable checkpoints.
Issue: #58. Branch is documentation-only; no source, ISO, system or
network deployment is authorized by this proposal.
Baseline: M10-A/B/C accepted and deployed in QEMU; release source
`8809c15f044f82d8e6c9afc8152980adff37d987`.
Governing references: `AGENTS.md`,
`docs/decisions/accepted.md`, `docs/decisions/open.md`,
`docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-task-scheduler-foundation.md`,
`docs/decisions/ADR-m9-3d6-irq0-delivery.md`, and
`docs/DEVELOPERS_GUIDE.md`.

## Context and decision request

The owner completed and accepted M10-A/B/C interactively on Forge and asked
to **start M10-D**. However, #58 previously marked implementation unapproved
until a separate architecture/acceptance decision; starting a design review
is not approval of a new ISR context-switch mechanism. A-006 requires
timer-driven preemption and Background, Normal, Interactive and
Time-sensitive priorities; A-013/A-029 require a supervisor recovery reserve.
The IRQ0 entry currently saves the complete 15-register GPR set and returns
by IRETQ. Cooperative M10-B context switches save only SysV
callee-saved state and MUST NOT be reused directly to resume an arbitrary
interrupted instruction. M10-C sets a coalesced request without scheduling.

## Proposed architecture — approval required

1. **Single boot CPU, ring-0 tasks, integer-only kernel execution.**
   Do not promise SMP, ring-3 entry, CR3 address-space switching or AVX/SSE
   task isolation. The existing C compilation constraints excluding
   floating point/SIMD must be verified, not assumed. Maintain the current
   interrupt controller and 100 Hz PIT0 setup with exactly one LAPIC EOI
   per real IRQ0. Preserve the M9 fallback boot path when IRQ routing fails.
2. **Independent interrupted-state representation.** Specify exact save
   layout for all 15 GPRs, RIP, CS, RFLAGS, and stack ownership, including
   the x86-64 distinction between same-privilege and privilege-changing
   IRETQ frames. The interrupted RSP must never be inferred from a
   cooperative call frame. Assembly must establish aligned C-call stack,
   with no red-zone reliance. Test frame construction and restoration
   independently before choosing a task during an IRQ.
3. **Deferred dispatch on IRQ exit, not inside the C tick callback.**
   IRQ0 C handler remains bounded: acknowledge/account, observe whether
   preemption is enabled and safe, and publish a request. After saving
   complete interrupted state and validating ownership, a dedicated,
   reviewed assembly IRQ-exit path may select a prevalidated next task
   frame for IRETQ. No heap, rendering, blocking or scheduler-list mutation
   from arbitrary nested interrupt context. Make reentrancy and
   interrupt-masked critical sections explicit, preserving the original IF.
4. **Safe ownership of stacks and lifetime.** Only RUNNING task may own the
   active execution stack; its saved trap frame lives until resumed or
   safely retired. Never free a running or just-interrupted stack while it
   is still active. Task exit and cleanup use a surviving dispatcher stack.
   Test partial frame-allocation rollback and a permanent supervisor
   descriptor/stack/CPU-service reserve.
5. **Four priority bands (policy to approve).** Proposal: stable FIFO
   round-robin within each band, higher bands preferred at dispatch with
   bounded per-band time-slice budgets; no lock held across a switch.
   Pure strict-priority is *not* sufficient because it can indefinitely
   starve Background and Normal tasks. Define bounded aging or periodic
   service windows with exact budgets and deterministic tests. Time-sensitive
   is a preference, not arbitrary hard real-time privilege. Supervisor
   dispatch must have reserved input/recovery access even if user work
   saturates the CPU.
6. **Compatibility/safety switch.** Default legacy boot/keyboard/editor
   workflow remains single-context until the new IRQ-exit tests pass.
   Enable preemption first in a test-only guest using two synthetic
   CPU-bound tasks, and only consider ordinary production tasks after
   demonstrating interrupt-frame safety and non-reentrancy. Never
   expose a knob that enables unreviewed scheduler state on release.

## Implementation gates and required evidence

- **D0 — Design acceptance:** owner approves interrupted-frame layout,
  dispatch location, priority/aging algorithm and supervisor budget,
  including current limitations. Update decision registers and API contracts.
- **D1 — Hosted pure-state tests:** trap frame layout/static offsets, queue
  invariants, priority fairness and aging, stale ID, exhaustion, supervisor
  quota, IF-preserving critical sections, no mutation on rejected requests.
- **D2 — IRQ-frame assembly fixture:** actual QEMU save/restore of all
  interrupted registers, RIP/RFLAGS/IF and stack alignment; malformed
  frame/failure paths keep existing safe boot or halt on uncertain IRQ state.
  No switching yet; no modification to production IRQ stub before D2 passes.
- **D3 — Test-only preemptive handoff:** two non-yielding kernel-mode
  CPU-bound tasks with separately owned 16 KiB mapped stacks get alternating
  CPU service under PIT0. Prove bounded switch latency, exact register and
  stack canaries, correct per-task IF, no double EOI, and cancellation/exit
  without use-after-free. Test 128/256/512 MiB and failures.
- **D4 — Policy & recovery stress:** all four priorities, starvation bounds,
  supervisor service under a CPU hog, blocked/wakeup transitions, no
  tick loss on long-running workloads, IRQ0-unavailable fail-closed mode,
  and non-reentrant editor/keyboard operation.
- **D5 — Complete regressions:** strict freestanding build; hosted sanitizers;
  M9 IRQ0 tick/negative, PIC/APIC/MMIO/IDT/TSS; M10-A/B/C tests; keyboard
  F1-F9, F10, timed F12; #UD and #DF; framebuffer exact pixels; logical
  editor scrolling/reflow at QEMU 128/256/512 MiB. Owner manually confirms
  interactive QEMU. No merge/deploy until separately authorized.

## Alternatives considered and risks

- Switch directly in `x86_irq0_handler`: rejected as current C entry has
  incomplete ownership information and arbitrary interrupted C is not
  reentrant. Nested lock acquisition or stack reclamation can deadlock or
  corrupt memory.
- Reuse M10-B callee-saved switch: rejected because volatile GPRs/RFLAGS
  and hardware IRETQ frame are different.
- Enable preemption for all current kernel activities immediately:
  rejected; editor, memory management, serial and other boot operations
  have not been audited for concurrent/reentrant access.
- Disable interrupts forever around the whole editor: safe but defeats
  preemption; use narrowly scoped critical sections, instrument and test.
- Strict four-level fixed priority: simple but unbounded starvation risk.
  Approval needed for an exact bounded-aging/quotas model.

## Explicitly deferred

Ring-3 processes, per-process CR3, FPU/SIMD live state, SMP, kernel-level
preemptible interrupt handlers, software timers, BIOS/physical laptop
testing, general driver thread-safety, physical media and any network
configuration are outside M10-D.

## Owner decision requested

Approve **this staged, test-first architecture** or request changes
before writing the D1/D2 implementation. Approval of this ADR does
not itself authorize merges, ISO deployment or physical hardware changes.

## Change history

2026-11-13: M10-A/B/C owner accepted; owner requested start M10-D.
Draft design authored for separate review while Issue #58's explicit
architecture acceptance gate remains open. No code changes.

2026-11-13: owner explicitly replied "i approve" to the proposed staged M10-D design in draft PR #62. This authorizes D1–D5 implementation and testing on isolated Forge checkouts, but does not authorize a merge or deployment. The architecture remains subject to exact trap-frame and priority-policy test evidence; unresolved behavior must be documented instead of assumed.
