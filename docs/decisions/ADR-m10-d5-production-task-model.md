# ADR M10-D5.1 — Production task lifecycle and ownership contract

Status: **PROPOSED CONTRACT, implementation authorization for staged D5 investigation and testing supplied in owner M10-D5 request (2026-11-13)**. No approval to merge, deploy, or enable ordinary-kernel preemption.

References: `AGENTS.md`, `docs/decisions/accepted.md`, `docs/decisions/open.md`, `docs/architecture/system-overview.md`, `docs/decisions/ADR-m10-task-scheduler-foundation.md`, `docs/decisions/ADR-m10-d-preemption-proposal.md`, `docs/decisions/ADR-m10-d4-priority-supervisor-proof.md`, `docs/DEVELOPERS_GUIDE.md`.

## Context and decision

M10-A provides a hosted-tested bounded table of 16 generation-tagged descriptors with a reserved supervisor slot 15, lifecycle invariants, and FIFO queue. M10-B provides cooperative x86-64 call-frame switching. D4 provides test-only IRQ-frame preemption, four-band selection, retained ready-time aging across short blocks, reserved supervisor dispatch, and bounded single-waiter priority inheritance. None provides the production task-execution and safe-reclamation contract.

**Recommended direction:** evolve the authoritative `kernel_task_table` and `kernel_task_handle` without copying the D4 controller into the normal runtime. Retain diagnostic code and QEMU flag unchanged until new hosted lifecycle tests and production-specific integration evidence exist. A workspace remains UI state rather than an execution context. The first D5.1 checkpoint is *metadata-only*, with no new IRQ switch, stack freeing, heap growth or editor concurrency.

## D5.1 task identity, states and record

- Preserve 16 bounded descriptors and exclusive supervisor slot 15 for the first checkpoint; capacity expansion requires separate resource and latency analysis. Preserve opaque `{slot, generation}` handles. Never wrap a generation. A terminated descriptor is not reusable while any saved frame, stack, wait reference, owned lock or reaper cleanup remains. Distinguish logical termination from physically reusable/free state.
- Keep existing enum names and transitions until migration tests demonstrate safe compatibility. `CREATED -> RUNNABLE -> RUNNING -> RUNNABLE`; `RUNNING -> BLOCKED -> RUNNABLE`; privileged termination may begin from CREATED/RUNNABLE/RUNNING/BLOCKED. TERMINATED becomes reclaimable only after a separate safety gate. A quarantine/failure state is proposed for later integration, not silently added to production execution.
- Proposed additional metadata, separately initialized and validated: immutable bounded name, base/effective priority, entry callback + opaque argument, execution-context kind and validated saved-frame ownership, stack extent/backing owner and lifetime phase, wait reason/object identity, parent/supervisor handle, dispatch count, execution ticks, and cancellation/cleanup disposition.
- Do not add unmanaged raw pointers to the exported public lifecycle API. Execution context and backing remain kernel-private owners with explicit attachment and teardown operations, rather than copied through structs. A task descriptor must never imply a valid execution context merely because its state says RUNNABLE.
- For all APIs, specify allowed caller context (bootstrap, running task, supervisor, timer IRQ), IF-preserving exclusion, rejection without mutation, and explicit bounded failure statuses. Task handle validation and global invariants precede state change.
- No heap, rendering, logging or stack reclamation in IRQ-exit selection. Reaper reclaims on a verified surviving stack; context lifetime may outlive logical termination.
- Reserved supervisor CPU service and memory must remain unavailable to ordinary creation. No automatic OOM task killing; task creation returns a recoverable error.

## Integration boundary

Keep M10-B cooperative context separate from the full saved IRQ frame. A real production dispatch transition must prove all GPRs, RIP, RSP, CS, RFLAGS/IF, alignment, and return frame ownership in QEMU before enabling it; use no unsupported SIMD/FPU or Ring 3 assumptions. D4's single-waiter mutex cannot be generalized into nested/multi-waiter locking. Production queues, wakeups and priority donation require a separate D5.3 contract.

## First independently reviewable implementation

1. Add **hosted-only** lifecycle/ownership metadata or sidecar state with deterministic initialization and invariant validation, preserving existing public functions and D4 build outputs.
2. Add tests for missing/invalid stack context, failed attachment and rollback, stale-generation rejection, capacity and reserved slot, illegal transition nonmutation, terminated-but-not-reaped descriptor reuse rejection, and double cleanup.
3. Run hosted ASan/UBSan and existing M10-A and D4 completion acceptance targets. Only after this may subsequent checkpoints attach actual execution stacks and expand scheduling behavior.
4. A normal unflagged QEMU ISO must remain behaviorally and binary-isolation verified against the currently accepted build where the build contract permits.

## Alternatives and tradeoffs

- Replace M10-A with D4 controller: rejected; D4 hardcodes four workers and one supervisor, has diagnostic identities, lacks full lifetime tracking and requires risky migration of existing tests.
- Grow arbitrary task tables and synchronization graphs immediately: deferred; complicates bounded IRQ work and OOM rollback.
- Treat TERM as immediately reusable: rejected; stale saved frames and wait references may still refer to its stack.
- Enable IRQ preemption across existing editor/kernel immediately: rejected; reentrancy and locking are unproved.
- Implement Ring 3 now: deferred to D5.6 boundary review; paging, TSS, syscall, CR3 and user-fault containment need separately accepted contracts.

## Acceptance and rollback

Acceptance requires hosted lifecycle tests with sanitizers, 128/256/512 MiB memory/failure checks where applicable, exact `make HOST_CC=gcc test-m10-d4-complete-acceptance`, new D5.1 target, green CI on exact PR SHA, and no regressions in ordinary keyboard/editor/F1–F12/exception behavior. Report PASS only with actual command evidence. Keep accepted normal/D4 ISOs and rollback copies unchanged; development occurs on a dedicated branch and disposable QEMU artifacts. No network/OPNsense or physical-drive changes.

## Unresolved owner decisions

No new product policy is needed to add isolated metadata tests under the existing accepted M10-A and D4 architecture. Before production activation, seek explicit signoff on the production scheduler contract, reaper ownership, task quotas and stack guards, priority quantum/fairness bounds, synchronization ownership/donation model, and any Ring 3 boundary. Merge/deployment remain separately approval-gated.

## History

2026-11-13: documentation-first D5.1 proposal based on current GitHub decisions and Forge inspection. QWEN execution from the available restricted Forge remote account is not yet available; no local or QEMU tests claimed for this proposal.
