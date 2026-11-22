# ADR M10-D5.9-C/D/E — Isolated multitasking preview

Status: owner-authorized development and disposable QEMU testing in the
November 2026 hands-on handoff. Parent draft PR #94 is e8938cb. Its
exact-head CI run #111 passed. Preview acceptance is pending.

## Decision

Extend the separate D5.6/D5.7/D5.9-B IRQ build under a dedicated preview flag.
Use one M10-A task table, one IRQ selector, the D5.2b ledger, and the existing
D5.7 guard. Two independent workers and the reserved supervisor retain their
owned stacks. A fourth exact-generation IRQ-return stack has sole runtime
ownership of PS/2 input, the decoder, editor lines and cursor state, the nine
workspace snapshots and the framebuffer. The existing boot dispatcher
survives on its own stack.

Initialize the editor and reserve exact snapshot backing for all nine
workspaces before binding the runtime. This keeps snapshot allocation out
of scheduled execution. An allocation failure aborts preview startup and
retains any already prepared pages until shutdown. Runtime copies use the
prepared pages, with IF enabled. The foreground publishes the active session
root via the existing guarded memory-service transaction. Workspace reset,
MMIO setup, allocator lifecycle and callbacks retain their boot-only gates.

Use elapsed monotonic IRQ0 time for the F12 overlay. Scheduled execution can
miss a wrap of the legacy polled PIT counter. F10 copies task progress,
IRQ0 and service metrics in one bounded, IF-preserving D5.7 scope, then
renders outside that scope. Drivers, workers and the supervisor never draw
to the foreground display. Account and acknowledge each timer IRQ once
through the existing handler before validating and selecting a return frame.

The interactive ISO runs until the QEMU window closes. A separately flagged
bounded test exercises selector disarm, return to the boot stack and exact
termination and stack retirement. Keep uncertain resources retained if
execution authority is lost. Test a real worker exception through the existing
fatal trap path. A fatal trap halts on the verified exception path and does
not attempt recovery or reclamation.

## Acceptance and rollback

Require real QEMU with 128, 256 and 512 MiB. Exercise independent worker
progress and completed supervisor service while checking punctuation, keypad,
Delete, logical-line and cursor behavior, F1–F9 pixel/state restoration,
coherent F10 metrics and F12 timing. Verify off-stack cleanup separately from
fault containment. Run GCC sanitizer checks, native and freestanding Clang,
source inventory, safe launcher regression, full inherited acceptance and
independent exact-head CI. Verify unchanged default stripped ELF and the
accepted D4 ISO hash. Record candidate path, source SHA, SHA-256 and launcher.

Close the preview and use the existing D4 launch path to return to accepted
behavior. Preserve PR #94 and the accepted ISO. This scope covers single-CPU
QEMU preview only. Ordinary editor preemption, Ring 3, SMP, PR merge, accepted
ISO replacement and physical deployment need separate decisions.

References: AGENTS.md, accepted.md, open.md, the system overview and developer
guide, ADR-m10-d59-production-preemption-boundaries.md,
ADR-m10-d59b-runtime-memory-wiring.md, ADR-m10-d57-preemption-and-priority.md,
ADR-m10-d58c-c2b-real-irq-bridge.md and ADR-m10-d5-resource-ownership.md.
