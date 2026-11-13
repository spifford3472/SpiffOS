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


## Hardened D3 negative and frame-pointer lifetime review

The coordinator reviewed the exact IRQ-frame handoff function with local
QWEN `qwen3-coder:30b` (narrow, read-only audit). QWEN raised possible
use-after-free, pointer bounds and stale bootstrap-frame concerns. **The
positive and negative QEMU evidence does not, by itself, establish
general-purpose protection against arbitrary corrupt kernel pointers.**
For the restricted D3 test, only the boot CPU runs; the IRQ gate clears IF,
the two separately owned stacks stay mapped until `d3_active` is disabled,
and the boot dispatcher releases them only after IRETQ returns. Thus
no other CPU or task can concurrently release a selected stack. The
implementation was nonetheless tightened to verify the worker-frame
pointer and complete 20-word extent are inside the owning stack BEFORE
reading saved SS/CS/RFLAGS; null and out-of-range task indices are rejected.
The boot frame is supplied by the trusted M9 interrupt-entry stub.

The hardened test now includes **nine** 128/256/512 MiB guest cases:
three genuine CPU-hog preemption successes with worker progress and
stack cleanup, three disabled-IRQ fail-closed cases, and three deliberate
corrupt-canary cases after returning to the boot stack, each requiring a
diagnostic error and refusing keyboard/success publication. These
negative tests do not emulate malicious ring-3 pointers or SMP races.

The first D3 compile failed because the existing stack ownership provider
was guarded for M10-B only; D3 now reuses the exact provider under a
narrow compile-time diagnostic OR. An initial boot failure
`CLOCK_PREPARE` was traced to M9's documented time-unavailable-before-
first-IRQ behavior, and corrected without weakening the final requirement
for actual elapsed hardware ticks. A malformed newline in a source
comment was separately caught by strict Clang compilation and corrected.
All corrections remained isolated; the owner ISO was never changed.


## Final independent release-gate evidence — 2026-11-13

At exact **tested code SHA**
`237fc2bcbfb392dab12aa4feb4c00f6a70467f79` on an isolated
Forge review checkout:

- `make test-m10-d3-guest`: PASS nine QEMU guest proofs at
  128/256/512MiB: three actual timer-preempted pairs of non-yielding
  workers with >=12 IRQ slices each and owned-stack release; three
  IRQ0-disabled negatives; three independently built, guard-corruption
  fail-closed negatives.
- `make HOST_CC=gcc test-m10-d3-acceptance`: **PASS**, log
  `/tmp/spiffos-m10-d3-hardened-full.log`,
  `SPIFFOS_M10_D2_ACCEPTANCE_OK` and
  `SPIFFOS_M10_D3_ACCEPTANCE_OK` verified; runtime ~160.51s.
  All D1 policy sanitizers, D2 real trap positive+negative,
  M10-A/B/C, memory/framebuffer exact pixels at three RAM sizes,
  keyboard/editor special characters, nine workspaces/F10/F12,
  positive/negative real IRQ0, #UD/#DF fault fixtures,
  APIC cache identity/negative and MMIO UC/NX/rollback passed.
  Real IRQ0 100-tick timing was 1.002 / 0.994 / 0.995 seconds.
- `make -B HOST_CC=gcc iso`: PASS, ordinary production build.
  Debug-stripped kernel ELF SHA-256
  `74fcf145423e0311366bc55223bd3a93cc24c46442f755183ba90b061bdac1fe`
  matched byte-for-byte the accepted M10-A/B/C executable
  (`cmp` PASS). No D1 priority, D2 probe or D3 handoff symbols were
  linked in the ordinary kernel. ISO container SHA differs because
  image packaging includes independently generated metadata; runtime
  ELF equality, not ISO checksum equality, is the verified claim.

GitHub comparisons between the tested code commit and the final
D3 branch show only additional ADR/operations documentation changes,
not a different compiled/tested D3 implementation. Final merge
and Forge deployment remain conditioned on an exact-main SHA,
a verified persistent backup of the current installed ISO
(`ffb0631bbbc0eb4b6885dfe46db158939f75a726c5f3d79fd57ddf9a6c78ab90`),
and postmerge isolated QEMU proof. Original M9 backup must be retained.
Only the D3 test ISO enables timer-driven handoff. Normal ISO
must retain M10-A/B/C runtime behavior. No D4 general priority/supervisor
CPU guarantees or preemptive editor support are claimed.
