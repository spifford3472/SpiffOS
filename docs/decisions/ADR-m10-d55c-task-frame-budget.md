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

Trusted multiworker IRQ-entry task authority binding, safe IRQ-exit dispatch integration, preempt-disable nesting/memory ordering, owner-death recovery, timer accuracy/jitter, SIMD/FPU, Ring 3/CR3 and separate production activation authorization.

## Change history

2026-11-14: owner chose M10-D5.5c after D5.5b; documentation-first isolated scope, no merge/deploy permission.


## Implementation and independently verified evidence — 2026-11-14

Draft stacked PR #80 on `feat/m10-d55c-task-frame-budget` above #79–#71. QWEN `qwen3-coder:30b` was used **first** for a narrowly scoped overflow-safe frame-span helper, completing in 34.76 s; coordinator reviewed its bounds logic and integrated the M10-A/registered-stack validator, hardware-tick monitor, real guest fixtures, tests and documents. No Codex.

**Exact M10-A frame binding:** `m10-d55c-frame-owner.[ch]` checks the existing M10-A `current_slot` and exact `(slot,generation)` RUNNING descriptor against an unquarantined D5.2b registered 16KiB stack, including full 20-qword frame span, saved RSP ownership and canonical saved RIP/RSP, ring-0 CS/SS, saved IF=1/live IF=0 and no lock/wait references. No task or resource changes in the policy itself. New test-only `SPIFFOS_M10_D55C_FRAME_GUEST_TEST` starts a genuine M10-A-owned worker on mapped, independently frame-owned stack, waits for actual M9 IRQ0 observer callbacks with real `x86-irq0.S` saved-frame R12 pointer, verifies **four** authentic frames in the *currently running* task generation, terminates the worker and releases its exact stack from the surviving boot dispatcher. `SPIFFOS_M10_D55C_BAD_GENERATION_TEST` changes only the claimed handle generation inside the observer; correct hardware frame/real task metadata remain untouched and the guest fails before editor entry.

**Independent non-yielding supervisor service budget:** `d55c_tick_service` monitor observes actual monotonically increasing IRQ0 ticks and only resets on completed supervisor work. `SPIFFOS_M10_D55C_TICK_TEST` instruments the preexisting *D4 diagnostic* non-yielding worker experiment with the tick monitor and records work performed by the surviving boot supervisor, **not** a new M10-A scheduler or general CPU realtime guarantee. A one-tick negative deadline `SPIFFOS_M10_D55C_BAD_BUDGET_TEST` fails closed without keyboard.

### Forge verification

- `make HOST_CC=gcc test-m10-d55c-frame-tick`: **PASS** with GCC `-Werror` ASan/UBSan, marker `SPIFFOS_M10_D55C_FRAME_TICK_OK`. Includes exact task generation/owner matching, forged pointer/frame/CS/IF/critical/lock negatives, clock rollback and **20,000** verified supervisor-service updates.
- `make HOST_CC=gcc test-m10-d55c-frame-guest`: **PASS in 14.76 seconds**, real 128/256/512MiB QEMU actual owned M10-A worker IRQ frames, four observations, safe termination/reaping and editor restoration. Forged-generation negative **rejected at all three memory sizes**, with actual CPU frame unchanged and editor not started. Initial broad `ACTUAL_FRAME_OR_CLOCK` failure was traced to requiring a startup clock value before the first timer IRQ; diagnostic labels were separated and the optional initial clock sample was handled correctly. No weaker task/frame validation was introduced.
- `make HOST_CC=gcc test-m10-d55c-budget-guest`: **PASS in 23.81 seconds** at 128/256/512 MiB. Max unserviced gap **four real IRQ ticks** for non-yielding D4 workers, at the 100Hz QEMU timer rate; deliberately one-tick deadline rejection also **PASS** at all three memory sizes. An initial negative harness marker was adjusted to the exact emitted D4 error label (no scheduler policy change).
- Full `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine make HOST_CC=gcc test-m10-d55c-acceptance`: **exit code 0 in 327.81 seconds**, isolated tree `/tmp/spiffos-d55c-20261114`, log `/tmp/spiffos-d55c-acceptance-final.log`. Marker `SPIFFOS_M10_D55C_ACCEPTANCE_OK` and inherited D5.5b, D5.5, D5.4, D5.3, D5.2d/c/b/a, D5.1, D4-complete/checkpoint, D3, D2 all PASS. Inherited M9 keyboard/console/editor/workspace, real IRQ0 128/256/512 MiB, exception #UD/#DF, APIC MMIO/cache/alias positives and fault-injection negatives, and actual cooperative/IRQ test stacks PASS.
- The unflagged normal debug-stripped ELF SHA256 `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb` **matches the established D4 normal executable baseline**. Disposable test-tree ISO SHA256 `9780cbac19b0d0fd7d469acade96146221542cd66e29183eeb3a048177a56798`, not a released ISO or image-byte-equality claim. Owner's Forge checkout `git status --short` empty; `-net none`, isolated QEMU/OVMF variables, no physical disk writes or accepted ISO replacement.

### Remaining production blockers

The single-worker IRQ observer verifies genuine exact-generation M10-A frame ownership, but **does not change the CPU task in IRQ**. General preemptive run-queue integration, lock-aware preempt-disable nesting, safe multiworker frame-switch transaction, task interrupt-context ownership refresh, exception/owner-death cleanup, SIMD/CR3/Ring 3 and physical realtime service guarantees remain open. The D4 non-yielding monitor is separately diagnostic and cannot be conflated with an authoritative M10-A CPU-time guarantee. No merge, physical install or normal editor preemption authorized. Exact final-head GitHub CI is a separate check.
