# M10-D5.7 — Production preemption-readiness audit register

Date: 2026-11-14. Status: **preliminary read-only source-path inventory, not certification of thread safety**.

Governing documents: `AGENTS.md`, `docs/decisions/accepted.md`,
`docs/decisions/open.md`, `docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-d57-preemption-and-priority.md`.

## Scope and observed architectural boundary

D5.7's actual IRQ-exit scheduling is compiled **only** when both
`SPIFFOS_M10_D56_IRQ_TEST` and `SPIFFOS_M10_D57_IRQ_TEST` are enabled.
The normal `spiffos-kernel.elf` does not link these objects. The D5.7
guard in `m10-d57-safety.[ch]` protects only the controlled D5.6 guest
critical sections; it does not rewrite or automatically intercept ordinary
SpiffOS kernel functions. Do **not** interpret a clean QEMU diagnostic as a
general driver/preemption signoff.

## Work required before enabling normal application preemption

| Source path(s) | Potential execution dependency | Required future gate / test |
|---|---|---|
| `src/kernel/kernel-task.c`, `kernel-dispatch-d54.c`, `kernel-sync-d53.c`, `kernel-task-resources.c`, `kernel-task-reaper.c` | Exact task lifetime, ready queue, lock/wait references, stack ownership | Verify every mutation runs in explicitly owned exclusion; prove no IRQ mutex mutation, post-IRQ exact saved-RSP refresh, owner death and no reclamation on running stack; real multiworker dynamic donation QEMU |
| `src/kernel/kernel-heap.c`, `memory-boot.c`, `frame-allocator.c`, `frame-allocate.c`, `memory-service.c` | Allocator/frame and page ownership, possible partially updated metadata | Build read-only call graph and preemptible/nonpreemptible allocation policy, prohibit allocation in ISR, add interrupt-injection tests at mutation boundaries, and prove rollback/oom |
| `src/kernel/keyboard.c`, `keyboard-ps2.c`, `keyboard-console.c`, `logical-line.c` | Keyboard event transport and ring/bounded input queues | Verify producer/consumer atomicity and IRQ interactions; ensure no dropped/duplicated key events during concurrent edit and worker load; retain special character, keypad and Delete regressions |
| `src/kernel/screen-editor.c`, `workspace-manager.c`, `text-console.c`, `framebuffer.c`, `console-font.c` | Shared editor lines, workspace state and display buffers | Define single writer or short preemption-disabled critical regions, separate rendering from IRQ; prove long-line wrap, cursor edits, F1–F10 switching and F12 status consistency under workload |
| `src/kernel/apic-irq0.c`, `kernel-timer.c`, `kernel-scheduler-clock.c`, `x86-irq0.S`, `x86-idt.c`, `x86-tss.c`, `x86-exceptions.S` | IRQ entry/exit, timer accounting, EOI, fatal faults, interrupt stacks | Audit IF and nesting per path; never double-EOI; prove preempt-disable + interrupt-return frame source/target; verify #UD/#DF on worker stacks and fail-closed supervisor recovery without invented frames |
| `src/kernel/x86-mmio-map.c`, `apic-memory-type.c`, `x86-page-walk.c`, `mmio-page-policy.c` | Page-table/MMIO state and CPU cache-type constraints | Determine live mapping mutability before adding preemption; prohibit IRQ mapping mutation; retain alias/cache/rollback tests and add concurrent use proof if mutations are enabled |

This inventory is based on paths visible in the source tree and architecture.
It does **not** assert all files are currently mutable concurrently or
that their present single-threaded logic is incorrect.

## Required sequencing and hard stop

1. First prove dynamic D5.3 mutex donation and wake under **real IRQ-
   preempted** M10-A multiworker execution with a surviving supervisor.
2. Perform a call-site-by-call-site read-only audit of each table row, with
   exact `file:function` evidence and an explicit ownership/exclusion
   decision. No blanket application of CLI or spinlocks.
3. Instrument one small functional subsystem at a time; run dedicated
   failure injection and the complete inherited keyboard/editor/IRQ0 suite.
4. Only after every mutation path is covered and current-frame corruption,
   orphan detection, forced-stop and deferred off-stack cleanup semantics
   are independently verified should normal EDIT/BASIC preemption be
   proposed for explicit owner review.

**Do not** attempt to release dead-owner locks automatically, retry
quarantined backing, infer realtime guarantees from dispatch count, or
modify physical devices, networks or the accepted ISO.

## Change history

2026-11-14: initial D5.7 inventory recorded concurrently with real-QEMU
safety tests; all production subsystem gates remain OPEN.
