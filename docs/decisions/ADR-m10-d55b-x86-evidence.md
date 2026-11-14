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
