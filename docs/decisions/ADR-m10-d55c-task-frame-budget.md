# ADR M10-D5.5c — Exact task-frame binding and non-yielding supervisor service proof

Status: owner authorized **isolated development/testing only**, 2026-11-14. Governing AGENTS.md, docs/decisions/accepted.md, open.md, docs/architecture/system-overview.md, ADR-m10-d55-preemption-safety.md, ADR-m10-d55b-x86-evidence.md, ADR-m10-d54-priority-dispatch.md, D4 full IRQ diagnostic and D5.2b resource registry.

## Problem

D5.5b reads the true hardware IRQ0 frame during the D2 assembly sentinel but cannot prove which authoritative M10-A generation owns that interrupted stack; D5.4 reserves dispatch *opportunities* without guaranteeing measurable supervisor service under non-yielding code. D4's independent four-worker demonstration measures an IRQ tick-based supervisor gap but is not linked to authoritative M10-A. Connecting them must not create a parallel task authority, permit IRQ-time locks, or enable the ordinary editor's preemption.

## Design

1. Create a bounded read-only **M10-A frame-binding verifier** that accepts the exact handle from the existing authoritative `kernel_task_table.current_slot`, the matching D5.2b registered stack owner, an immutable 20-qword real IRQ0 trap frame from the trusted entry stub, and checked privilege/critical metadata. Require exact generation, RUNNING state, stable full 160-byte frame inside registered stack, saved RSP inside the same stack (not equal to handler's current stack), canonical 4-level RIP/RSP, ring-0 expected CS/SS, saved IF and reserved RFLAGS bit. Require a consistent stack snapshot and no owned locks/waiters for IRQ preemption. Never mutate the frame, table, registry or scheduler. Treat all arguments as untrusted except those supplied by the future trusted x86 IRQ entry adapter. Hosted tests are negative invariants; test-only guest must exercise a real hardware frame on an actually registered M10-A worker, or record the gap explicitly rather than claiming completion.
2. Implement a bounded **hardware-tick-derived supervisor service monitor** with latch-on-miss. Count observed IRQ0 ticks independently from the scheduler decision sequence; record service *completion* only from the verified supervisor context after it has run. Fail on clock rollback/unavailable samples or elapsed ticks beyond configured budget. The existing D4 non-yielding worker experiment is a useful real-IRQ baseline; it does not establish a production hard realtime guarantee. Add a distinct QEMU diagnostic with non-yielding worker and actual supervisor service timestamp checks if feasible without replacing M10-A.
3. Preserve single-core, no allocation, IRQ=0 critical sections for mutation, no SIMD/CR3 changes, fail-closed on corrupted or stale references, and never turn on ordinary-editor preemption.
4. Keep test-only objects behind an explicit flag and provide hosted GCC ASan/UBSan, three-size QEMU and inherited D2–D5.5b regression. Compare the normal stripped ELF against established D4 baseline; preserve owner checkout/ISO.

## Refused shortcuts

Do not equate an address merely *within* a stack with proof of a trusted current task, treat an arbitrary caller-supplied task handle as authentic, use D4 diagnostic task state as second production authority, let ISR release a mutex or task stack, or claim tick-based service monitoring guarantees hard CPU-time deadlines on physical hardware.

## Open

Trusted IRQ entry + exact authority binding, real owned-worker interrupt-frame execution, safe IRQ-exit dispatch integration, preempt-disable nesting/memory ordering, owner-death recovery, timer accuracy/jitter, SIMD/FPU, Ring 3/CR3 and separate production activation authorization.

## Change history

2026-11-14: owner chose M10-D5.5c after D5.5b; documentation-first isolated scope, no merge/deploy permission.
