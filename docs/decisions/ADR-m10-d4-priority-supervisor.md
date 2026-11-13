# ADR M10-D4 — test-only four-class IRQ scheduling and supervisor service

Status: **OWNER REQUESTED IMPLEMENTATION FOLLOWING D3.1**, 2026-11-13.
Scope: real PIT0 timer IRQ-exit task selection in isolated QEMU test builds,
not full production editor/process preemption.

Governing references: `AGENTS.md`, `docs/decisions/accepted.md`,
`docs/decisions/open.md`, `docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-task-scheduler-foundation.md`,
`docs/decisions/ADR-m10-d-preemption-proposal.md`,
`docs/decisions/ADR-m10-d31-workspace-diagnostics.md`,
`docs/operations/m10-d3-irq-handoff-20261113.md`,
`docs/operations/m10-d1-priority-policy-20261113.md`,
and `docs/DEVELOPERS_GUIDE.md`.

## Decision and safety boundaries

M10-D3 already proved real IRQ0 full-frame handoff between two CPU-bound
16KiB independent stacks and the surviving boot dispatcher; M10-D1
tested four priority classes, wait-age preference and every-fourth
reserved supervisor selection purely in hosted code. D4's first gate
integrates that **same tested selector** into D3's test-only IRQ-exit
callback and tests its decisions in actual QEMU, without modifying the
one-EOI real IRQ C handler or enabling generic editor preemption.

The D3 boot dispatcher maps to supervisor policy slot 15; D3 workers
map to policy slots 0 and 1. Each task has independent saturating
dispatch-wait age. Every selected dispatch resets its wait age; each
other runnable task gains one, saturating at UINT64_MAX. The selector
is called only on one boot CPU with IF=0 after the normal hardware
IRQ0 C timer callback and LAPIC EOI. It cannot allocate, log, call
the cooperative switch or mutate the system's general task table.
Only the test's static, prevalidated saved frame pointers are returned
for the assembly IRETQ epilogue. One out of every four completed IRQ
dispatch opportunities must select the reserved supervisor while it
remains runnable. A task that selects itself is a valid decision and
simply returns to the same saved frame.

First gated real-QEMU test uses two never-yielding CPU hogs; worker B
stays Time-sensitive while worker A cycles through Background, Normal,
Interactive, Time-sensitive in four deterministic 80-tick epochs.
This tests all priority definitions and anti-starvation under sustained
high-priority load, but not all four classes simultaneously, which
requires a future four-worker stack-provider extension. The boot
supervisor must regain service regularly; check the largest observed
wait interval and each epoch's worker progress. Use a bounded test
deadline, strict per-frame owner validation, IF-preserving boot cleanup,
and injected invalid-priority negative to exercise fail-closed behavior.

## Explicit limits

This first D4 checkpoint does not implement ring-3, SMP, FPU/SIMD,
CR3/process switching, general user task syscalls, automatic preemption
of editor/driver code, sleep/wakeup queues or a hard real-time guarantee.
Priority-based scheduling remains **diagnostic only**. A further D4
gate must test simultaneous four-class runnable workloads, block/
wakeup transitions and measured supervisor service under heavier
resource contention before the whole D4 milestone can close.
D5 remains independent.

## Validation gates

1. Positive hosted D1 sanitizer tests and deterministic invalid priority
   rejection.
2. Real 128/256/512MiB QEMU D4 build: actual IRQ preemption, four phases,
   no yield, all worker phase service, one-in-four supervisor selection,
   bounded waiting, no altered hardware EOI count, stack canaries and
   owner cleanup, return to KEYBOARD_READY.
3. Bad-priority and IRQ-disabled negatives fail closed at all RAM sizes.
4. D3 positive/negative, D2 frame/negative, memory, keyboard/F1–F12,
   #UD/#DF, PIT IRQ0, APIC/MMIO and M10-A/B/C regressions remain PASS.
5. Complete exact-SHA documentation, PR and no owner ISO mutation.
   Merge/installation requires separately verified scope and approval.

## History

2026-11-13: owner approved D3.1 then moving to D4. QWEN completed a
narrow read-only integration review; a stacked isolated branch records
the first test-only D4 policy integration.


## D4 second gate: simultaneous four-priority CPU hogs

The next separately tested checkpoint extends **test-only** stack
ownership from 2 to 4 independent 16KiB four-page stacks, with a
fifth permanent supervisor dispatcher context. The ordinary D3,
two-worker D4, and normal ISO builds retain their original
2-stack fixture/ABI through a build-time flag. Worker A=Background,
B=Normal, C=Interactive, D=Time-sensitive all remain runnable
simultaneously under the real PIT0 IRQ0 path. All four must receive
nonzero, bounded-latency CPU service despite the high-priority
competitor. Every fourth dispatch still chooses reserved supervisor
slot 15. Each worker's full saved IRETQ frame, stack canaries,
progress and ownership is validated; partial allocation failure
must unwind without losing tracked frames. Deliberate malformed
priority, unavailable IRQ and stack-canary negatives must halt
without reporting success or permitting the normal editor to
enter with damaged state.

This is separate from blocked/wakeup and recovery-under-failure
acceptance, which remain open after the simultaneous-class gate.
