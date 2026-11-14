# ADR M10-D5.7 — Preempt-disable contract, donated-priority IRQ admission and fail-closed recovery

Status: owner elected M10-D5.7 isolated implementation on 2026-11-14. This is an incremental **test-only safety checkpoint**, not permission to enable preemption of BASIC, EDIT, drivers or the normal kernel. Governing AGENTS.md; accepted/open, system overview, developers guide; ADR-m10-d56-irq-handoff.md; ADR-m10-d53-bounded-synchronization.md; ADR-m10-d54-priority-dispatch.md; ADR-m10-d55-preemption-safety.md.

## Design and scope boundaries

M10-D5.6 proved two real non-yielding M10-A workers and a supervisor can switch at IRQ exit in QEMU. It uses a single global IF=0 critical counter, FIFO selection, and prevalidated frames, but this is not a production-wide critical-section system.

Work must be split into independently verified gates:

1. **D5.7a — strict preemption guard.** Add a fixed-capacity, single-CPU test-only guard that requires trusted IRQ/IF provenance and an exact current task generation for every enter/exit. Nesting must be LIFO and overflow/underflow/stale-handle/IRQ entry must latch a permanent failure; no attempted recovery by forced unlock or stack free. The IRQ selector is permitted to switch only at depth zero and only when its genuine interrupted task ownership is known. The guard is a diagnostic policy; it does not automatically instrument every real driver.
2. **D5.7b — priority-aware IRQ *selection*.** Reuse D5.3 donation and D1 bounded aging/supervisor reservation. Read D5.3 lock graph and M10-A ready queue under IRQ=0 without calling D5.3 mutating APIs inside IRQ. The selected exact READY handle is handed to the existing M10-A `kernel_task_irq_handoff` — no parallel lifecycle manager. Validate rollback on invalid frame, stale generation, quarantine or corrupt synchronization graph before any state mutation. Never ignore waiters or orphaned owners.
3. **D5.7c — failure containment.** A corrupt destination frame or stale identity must be rejected without altering the authoritative task table. A valid reserved supervisor context may receive control for diagnosis; no automatic lock-release, memory free or task resurrection inside IRQ. If current frame itself is unsafe, stop selecting new contexts and leave recovery to separately reviewed trap policy. Do not conceal an invalid source by guessing a fallback.
4. **QEMU acceptance.** Retain real 128/256/512MiB D5.6 guest, repeat with valid priority class selection and legitimate IRQ frame ownership. Inject a stale handle, corrupt frame and active guard; prove rejection before unsafe IRETQ; record actual serviced supervisor ticks. Hosted sanitizer tests must prove donated-priority outcomes, guard churn and unchanged state on all denial paths. Run inherited D2–D5.6 regression.
5. **Normal executable isolation.** New guest flags only; no normal editor/BASIC preemption, no physical disk, deployment or network changes; normal debug-stripped ELF hash must equal D4 baseline. Stacked draft PR, no automatic merge.

## Nonnegotiable technical distinctions

- A pure priority selector may run at the verified IRQ exit, but D5.3 acquire/release/cancel, resource mutation, allocation and cleanup remain **non-IRQ operations** with exclusion. Do not silently call D5.4's non-IRQ `d54_dispatch_next` inside the ISR.
- The diagnostic global guard cannot prove production-wide lock safety; all kernel locks/drivers, IRQ event queues, allocations and exact preempt-disable memory ordering require later targeted audits.
- Supervisor opportunities alone are not a physical hard realtime service bound.
- No self-healing, forced owner-death release, retrying quarantined stacks, Ring 3, CR3, SIMD or other-CPU preemption.

## Approval / history

2026-11-14: owner authorized investigation, implementation and tests of these safety boundaries; merge and physical release remain unapproved.
