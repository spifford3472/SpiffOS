# ADR M10-D5.5b — Hardware-observed IRQ0 frame evidence (diagnostic only)

Status: owner authorized the next **isolated development/testing** checkpoint on 2026-11-14. Governing: AGENTS.md, accepted/open, system-overview, ADR-m10-d55-preemption-safety, ADR-m10-d54-priority-dispatch, ADR-m10-d4-priority-supervisor-proof, M10-D2 frame definition and M9 IRQ0 assembly.

## Problem and architectural boundary

D5.5 policy uses caller-provided CPU evidence; it must not be called a hardware-authenticated preemptive execution gate. The real x86 IRQ0 stub already saves 15 GPRs and CPU RIP/CS/RFLAGS/RSP/SS (M10-D2) and the normal IRQ0 timer path performs EOI. The next minimal step is to **observe and validate** that real frame without substituting a second scheduler or enabling IRQ0 switching in ordinary SpiffOS. Do not turn the D4 diagnostic controller into a production owner.

## Decision

Introduce a separate `SPIFFOS_M10_D55B_IRQ_EVIDENCE_TEST` build flag. Under this flag only, the existing IRQ0 assembly passes the actual saved-frame pointer to a new **read-only** hook immediately after ordinary `x86_irq0_handler`. The hook collects no fabricated worker ID, does not switch CPU stacks, does not acquire locks, and does not allocate/log or call the regular D5.5 task admission API inside IRQ. Validate exact 20-qword frame layout, 8-byte frame alignment, saved CS/SS, canonical saved RIP/RSP, saved IF=1 for interrupted code, sampled actual C-entry IF=0, same-CPU stack bounds for the known test-owned boot stack, and bounded successive tick progress. Any mismatch latches a fail-closed diagnostic flag rather than returning a modified frame. The hook never alters registers or IRQ0 timer semantics. On return to the boot diagnostic, query counters and emit a stable PASS or diagnostic failure serial marker; keyboard must remain usable. Negative test corrupts the *expected* fixture identity (not actual trap state) and must reject without touching a saved IRQ frame.

This is proof of **actual CPU-saved interrupt-state provenance for the diagnostic only**, not full production current-task authority or a trusted context switch. Verification of the real CPU RSP/frame under a worker-owned stack, preempt-disable critical nesting, saved SIMD/CR3/FS/GS, IRQ reentrancy and supervisor *CPU time* bounds under non-yielding workloads remain OPEN. No ordinary editor preemption and no physical deployment/merge authorized.

## Acceptance

Host negative/format tests where feasible; test-only real QEMU at 128/256/512 MiB, IRQ0 100Hz and actual frame marker, corrupt-expected negative case, no frame mutation, regular editor/keyboard return, full inherited D2–D5.5 suite, normal ELF unchanged checksum, exact-head CI. All test source and documentation in a draft stacked PR.

## Change history

2026-11-14: owner selected D5.5b trusted x86 interrupt-context verification recommendation; design constrained to diagnostic-only observed frames before any scheduler activation.


## Implementation and validation record — 2026-11-14

Draft stacked PR #79 on `feat/m10-d55b-x86-evidence`, based on #78 through #71. Added `m10-d55b-evidence.[ch]`, a read-only hook called from the **actual** saved-frame R12 pointer in the existing IRQ0 assembly stub **after** its normal handler/EOI, only under `SPIFFOS_M10_D55B_IRQ_EVIDENCE_TEST`. It reuses D2's independent register-sentinel assembly and full-frame GPR validation, and additionally checks live IF=0, CPU-saved IF=1, assembly-recorded RSP/CS/SS/RFLAGS, bounded spin RIP, canonical RIP/RSP for the test-only 4-level QEMU mode and at least three IRQ observations. Negative build `SPIFFOS_M10_D55B_BAD_EXPECTED_TEST` changes only the observer's expected saved RSP and refuses the boot diagnostic without touching the CPU frame. No IRQ0 context switching, EOI, allocator, logging or normal kernel linker changes.

- Local Forge QWEN `qwen3-coder:30b` used first for narrow four-level 48-bit canonical address helper, **PASS in 33.69 seconds**; coordinator reviewed and integrated its bit-sign-extension logic, IRQ hook, QEMU fixture and regression wiring. Codex not used.
- `make HOST_CC=gcc test-m10-d55b-guest`: **PASS in 14.88 seconds**, real QEMU 128/256/512MiB positive observed frames and negative **wrong expected RSP rejected before keyboard** at all three sizes. Saved CPU frame remains untouched; editor restored in the positive build. No physical disk/network.
- Full `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine make HOST_CC=gcc test-m10-d55b-acceptance`: **exit 0 in 285.55 seconds**, disposable tree `/tmp/spiffos-d55b-20261114`, log `/tmp/spiffos-d55b-acceptance.log`. All D5.5b/D5.5/D5.4/D5.3/D5.2d/D5.2c/D5.2b/D5.2a/D5.1/D4-complete/D4-checkpoint/D3/D2 markers PASS, including original D2 positive/negative CPU frames, M10-D4 non-yielding-worker QEMU, M10-B/C, keyboard/editor/workspace, real IRQ0 timing/disabled, #UD/#DF, APIC/MMIO positives/negatives.
- Normal **debug-stripped ELF** SHA-256 `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`, **unchanged from established D4 normal executable baseline**. Disposable ISO SHA-256 `b3230959dd866ad3c10a2ae132975bc5f182c79640d9b6ef4baf982210dc6566`, not an accepted release artifact or ISO byte-equality claim. Owner's usual Forge checkout `git status --short` empty. QEMU uses `-net none`, isolated source/OVMF, no physical storage.
- Exact final-head GitHub Actions remains a **separate** gate. No merge/deployment authorization was requested or implied.

**Scope caveat:** Actual saved CPU frame provenance is verified for the D2 assembly sentinel, **not** for a particular live M10-A generation-tagged worker in an IRQ-preemptive dispatcher. The D4 separate non-yielding scheduler remains diagnostic only; a measured dispatch-count bound does not prove CPU-time supervisor service. Future work must authenticate worker frame identity/stack from actual IRQ entry, integrate D5.5 admission with the sole authoritative M10-A task owner, and bound supervisor service under non-yielding workloads before ordinary program preemption can be enabled.
