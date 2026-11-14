# ADR M10-D5.5 — Preemption admission, supervisor deadlines, and fail-closed fault gates

Status: 2026-11-13, user directed next safety-boundary phase after M10-D5.4. **Isolated development and tests only**; no generalized preemption, merge, accepted ISO, Ring 3 or physical device changes. Read AGENTS.md; accepted/open decisions; architecture overview; ADR-m10-d54-priority-dispatch.md; ADR-m10-d53-bounded-synchronization.md; ADR-m10-d4-priority-supervisor-proof.md; ADR-m10-d5-resource-ownership.md; ADR-m10-d5-supervisor-cleanup.md.

## Context

D5.4 genuinely selects authoritative M10-A tasks with donated priorities, and three real QEMU cooperative workers switch stacks. However its supervisor service reservation is measured in dispatch opportunities, not CPU time, and held mutexes/cleanup cannot safely run in IRQ context. D4 IRQ-exit handoff uses a separate diagnostic controller. Enabling actual general preemption could strand a kernel lock owner, transfer a partially saved context, reenter a memory owner, starve the supervisor or misidentify stale handles.

## Decision: narrow safety admission boundary first

Introduce a small **pure, freestanding, fixed-capacity safety admission module** that never switches context and never mutates tasks or locks. Its input is an explicitly captured snapshot of the existing authoritative task and resource state, execution provenance and an independent monotonic clock. It validates all present task generations, context stack ranges, lock reference counts, current CPU identity, interrupted frame constraints, dispatcher/supervisor identity, and exact provenance of requested operation.

Interrupt/execution admission:
- A switching request must originate from an interrupt-return or a documented cooperative safe point; reject nested IRQ or wrong context source.
- Releasing lock/owner backing, cancellation, mutex mutation and supervisor reaping must require **non-IRQ dispatcher/supervisor context with IF=0**. Reject ordinary worker, nested critical section, stale task, unresolved registered locks/waiters, potentially current stack release.
- Do not presume the request's claimed caller role is authentic; require an external trusted x86 entry adapter to substantiate caller identity and observed CPU state before using the gate to authorize any real mutation. This checkpoint tests admission *logic*, not a hardened hardware caller identity mechanism.

Supervisor CPU service:
- Record a real monotonic clock **sample provided by a trusted platform layer**, a configured nonzero maximum interval, and the time of a verified supervisor service completion. Saturating arithmetic and exact comparisons; reject time rollback, missing clock, or unserviced deadlines. A missed deadline sets a **latched diagnostic fault** and refuses preemption activation. No claim of realtime guarantee from this measurement alone. Clock sampling, IRQ progress, automatic recovery and CPU-time preemption remain open.
- A supervisor dispatch opportunity without an observed service-completion does **not** reset the service clock.
- No automatic clearing of latched missed deadline, no watchdog-triggered lock or memory release, no invisible CPU loop.

Failure containment:
- Invalid/suspicious contexts reject without modifying authoritative task table or resource registry. The diagnostic record may latch a fault, but never manufacture a runnable task or reallocate quarantined backing.
- All checks are bounded, single CPU, no allocation or I/O, and avoid normal-kernel linkage. An explicit future owner review is required before making these safety policies operational.

## Validation

GCC ASan/UBSan hosted: safe cooperative/IRQ-return gate admissions, nested IRQ reject, IF flag enforcement, lock-held worker rejection, stale generation and active-stack rejection, real time deadlines, loss/rollback/unavailable clock, supervisor service vs mere dispatch opportunity, diagnostic fault latch/no reset, 20,000 state changes and exhaustive invalid source matrix. Run full inherited D5.4 real 128/256/512MiB QEMU, editor/keyboard/exception/IRQ0 tests, normal stripped ELF hash gate and exact-head CI. No normal runtime modification.

## Rejected alternatives

Do not turn on production IRQ0 scheduling, treat FIFO reservation as a CPU realtime bound, infer safe lock recovery from zero counters, force-release an orphaned owner, restart a potentially corrupted task, or silently reuse the D4 diagnostic task controller as production authority.

## Open decisions

Verified x86 IRQ-entry identity/frame provenance and ABI; precise preempt-disable accounting and compiler/runtime memory ordering; true CPU-time supervisor budget with bounded non-yielding workers; deferred cleanup on safe stack; watchdog hardware coverage; deterministic owner-death recovery; FPU/SIMD/CR3/Ring 3 and inter-CPU semantics remain future milestones.

## Change history

2026-11-13: started M10-D5.5 as documentation-first test-only safety boundary after D5.4's final CI success, with no owner merge or deployment permission.


## Actual implementation and Forge validation — 2026-11-13

Draft stacked PR #78, branch `feat/m10-d55-preemption-safety`, based on M10-D5.4 PR #77 and earlier D5 drafts. Added `src/kernel/kernel-preempt-safety-d55.[ch]`: non-mutating task-generation/registered-stack admission for cooperative/IRQ-return switching; IF=0/IRQ-depth/critical-depth checks; fail-closed prohibition on IRQ synchronization edits and privileged off-stack reaper/cancel actions; independent monotonic service monitor with explicit verified completion and a permanent diagnostic fault on deadline expiry, missing sample or time rollback. **This module receives simulated/caller-provided evidence; it does NOT authenticate CPU RFLAGS, IRQ depth or RSP or change real interrupt scheduling.** Normal kernel linking is unaffected.

- Local Forge QWEN `qwen3-coder:30b` completed a narrow rollover-safe deadline helper in **32.90 seconds**. Coordinator integrated stricter clock-failure and permanent-latch semantics, wrote remaining C/tests/docs. Codex not used.
- Hosted `make HOST_CC=gcc test-m10-d55-safety` **PASS** under GCC ASan/UBSan `-Werror`, marker `SPIFFOS_M10_D55_PREEMPT_SAFETY_OK`. Tests worker vs dispatcher vs IRQ origin, IF/IRQ/critical nesting, active stack vs off-stack RSP, lock-held interrupt refusal, stale generation, missing/rolled-back time, exact deadline, permanent fault, and **20,000** verified supervisor-service cycles.
- Full isolated Forge `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine make HOST_CC=gcc test-m10-d55-acceptance`: **exit 0 in 273.61 seconds**, log `/tmp/spiffos-d55-acceptance.log`, disposable tree `/tmp/spiffos-d55-20261113`. Markers `SPIFFOS_M10_D55_ACCEPTANCE_OK`, D5.4/D5.3/D5.2d/D5.2c/D5.2b/D5.2a/D5.1/D4-complete and checkpoint/D3/D2 all present. Includes real QEMU 128/256/512MiB, three-worker multiwaiter handoff from D5.4, editor/keyboard/workspace, IRQ0 timing and disabled, #UD/#DF, APIC and MMIO positive/negative.
- Normal debug-stripped kernel ELF SHA-256 `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb` **identical to established D4 executable baseline**. Disposable normal ISO SHA-256 `4037ab8d3716e34e02eedfeb6c8f822dd73b7110f35e669f8f39ca6b4ad7a081`; not a released ISO or byte-identical ISO claim. Owner checkout `git status --short` empty. QEMU `-net none`, temporary OVMF variables, no physical drives, no accepted ISO replacement.

**Remaining gates before real preemption:** trusted assembly-backed provenance and interrupt/context save frame, preempt-disable atomicity/lock interaction, independent CPU-time supervisor bound with non-yielding tasks, actual real-QEMU admission use on observed CPU state, exception and owner-death recovery, SIMD/CR3/Ring 3 and separate owner authorization. Exact final-head GitHub CI is separate and must be verified. Do not enable ordinary editor preemption.
