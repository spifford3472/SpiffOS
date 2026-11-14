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
