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


## First verified real handoff — 2026-11-13

On isolated Forge detached checkout at code SHA
`8d80371f2fb1abf69f478b4407ecfd6fc43be82c`:
`make test-m10-d3-guest` **PASS**. Positive real PIT0 handoff
tests passed at 128, 256 and 512 MiB, including two non-yielding
CPU-bound stack owners, 12 or more IRQ slices each, progress of both
workers, boot-stack restoration, canary checks and owned backing release,
followed by `SPIFFOS_KEYBOARD_READY`. Negative disabled-IRQ tests at
128/256/512 MiB refused to activate any preemption and stopped with
`SPIFFOS_M10_D3_PREEMPT_ERROR:IRQ0_NOT_ENABLED`.
The non-trivial integration checkpoint is a gated three-context
test dispatcher only; it does not yet integrate the four-band D1
priority selector or provide a general-purpose task syscall/ABI.

The first build failed because the owned-stack service was originally
gated only to M10-B; the D3 diagnostic now reuses the same provider by
a compile-time OR guard (no duplicate allocator). The first guest boot
reported CLOCK_PREPARE: M9 does not publish elapsed time until the
first real IRQ0, so D3 now tolerates a pre-first-tick clock absence but
requires elapsed time and real ticks by completion. Both corrections
were retested in QEMU.

## Full regression command

```sh
make HOST_CC=gcc test-m10-d3-acceptance
```

This first runs positive/negative real preemption, then the D2
comprehensive suite (D2 trap/negative, D1 priority, M10-A/B/C,
128/256/512MiB memory and framebuffer, keyboard/editor F1–F12,
real IRQ0 timing and disabled route, CPU #UD/#DF, APIC and MMIO).
Only an entirely green result plus an independent exact-source SHA
and protected rollback should permit owner-authorized release.

## Default-kernel boundary

No D3 code is linked to the normal ISO without
`SPIFFOS_M10_D3_PREEMPT_TEST`; its path uses the existing M9 IRQ0
callback/EOI and plain IRETQ without a switch. Staging, testing and
release must not replace the owner ISO without a verified original
checksum and a persistent rollback. Physical boot media are forbidden.
