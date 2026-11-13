# M10-D3 isolated IRQ-exit handoff proof — implementation plan

Status: owner authorized implementation, merge and Forge QEMU deployment **only after verified gates** (2026-11-13). In-progress test-only branch; the approved M10-A/B/C ISO must remain unchanged until a verified candidate and rollback exist.

Governing references: `AGENTS.md`, `docs/decisions/accepted.md`, `docs/decisions/open.md`, `docs/architecture/system-overview.md`, `docs/decisions/ADR-m10-task-scheduler-foundation.md`, `docs/decisions/ADR-m10-d-preemption-proposal.md`, `docs/decisions/ADR-m9-3d6-irq0-delivery.md`, `docs/DEVELOPERS_GUIDE.md`.

## Isolated contract

- Two non-yielding ring-0 CPU-bound tasks; their independent 16 KiB mapped 4-frame stacks are allocated/released under IF=0 with existing M10-B audited owner API. Preserve supervisor reserve and allocation failure rollback.
- The CPU's unmodified real external IRQ0 gate saves full 15-GPR + five-word long-mode RIP/CS/RFLAGS/RSP/SS frame, via M9 assembly. Existing C IRQ0 accounts exactly once and emits the LAPIC EOI. Only **after** that, a test-gated IRQ-exit callback receives the saved frame pointer and returns a validated next frame pointer to the assembly IRETQ epilogue. Neither the handler nor callback allocates, frees, logs, locks, renders or invokes the cooperative switch.
- The dispatcher boot stack joins the test rotation as a third context, allowing clean retirement after the task stacks are no longer active; the task routines never voluntarily yield.
- A fresh first-run frame is built high in each stack with 16-byte SysV entry alignment, correct saved CS/SS/RFLAGS.IF, and RIP pointing at a noreturn worker; task-return path fails closed. Saved frames are retained on their owning stacks until next dispatch.
- No live keyboard/editor preemption. Diagnostic runs after BOOT_OK and returns to original keyboard loop if all gates pass. Default ISO keeps existing IRQ0 exact behavior. Ring3, SMP, CR3, FPU/SIMD and D4 priority enforcement remain deferred.

## Required acceptance

Real QEMU 128/256/512MiB: both tasks execute without cooperative yields, each receives multiple timer-driven slices, true cross-task handoffs, monotonic accounting, intact stack canaries and saved full frame, correct RFLAGS.IF, clean return to dispatcher, exact backing-frame cleanup, and return to KEYBOARD_READY. Test allocation rollback and a disabled-IRQ negative. Re-run D1, D2 positive/negative and complete existing memory, keyboard, IRQ0, #UD/#DF, APIC/MMIO, M10-A/B/C regression tests before merge/deploy. Script must never overwrite owner ISO.

## Change history

2026-11-13: Owner explicitly requested development, merging and deployment once safe. QWEN reviewed a narrow IRQ-exit pointer contract; coordinator will require independent real QEMU evidence. This document is the design-first foundation for the D3 branch.
