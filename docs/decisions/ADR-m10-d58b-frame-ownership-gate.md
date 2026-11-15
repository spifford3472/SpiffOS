# ADR M10-D5.8b Phase 1 — Lock-aware frame admission and blocked-stack boundary

Status: owner directed isolated Phase 1 implementation and tests on 2026-11-14. This is **not** authority to modify D5.6 IRQ dispatch, deploy or merge, or enable normal EDIT/BASIC preemption.

## Governing references and actual blocker

`AGENTS.md`; `docs/decisions/accepted.md`; `docs/decisions/open.md`; `docs/architecture/system-overview.md`; `docs/DEVELOPERS_GUIDE.md`; `ADR-m10-d53-bounded-synchronization.md`; `ADR-m10-d54-priority-dispatch.md`; `ADR-m10-d55c-task-frame-budget.md`; `ADR-m10-d56-irq-handoff.md`; `ADR-m10-d57-preemption-and-priority.md`; `ADR-m10-d58-irq-mutex-integration.md`; and `docs/architecture/m10-d57-production-preemption-audit.md`.

The current `m10-d56-guest.c:frame_safe()` and `m10-d55c-frame-owner.c:d55c_frame_matches()` reject nonzero `owned_locks` **or** `registered_waiters`. That rejects a valid RUNNING/RUNNABLE mutex owner during real IRQ selection. But simply deleting either test would admit a forged ledger or blocked waiter, violating source/destination frame ownership. M10-A is the sole lifecycle authority; D5.3 is the sole bounded lock graph, and D5.2b binds exact-generation stack ownership. Neither can be replaced by a second scheduler or unchecked reference counts.

## Decision: separate proof without enabling the IRQ bridge

1. Implement a **pure, read-only, bounded** D5.8b reference-admission validator. Input: M10-A task table, D5.2b resources, D5.3 graph, exact handle, required state (RUNNING or RUNNABLE only), and a verified interrupt/IF provenance witness. Validate `kernel_task_resources_check()` and **complete** `d53_check()` (including generation-matched profiles, owner/waiter membership, counter equality, ranked wait chains and orphan handling), then confirm the exact task state, installed unquarantined stack owner, and no registered waiters for a runnable task. A task may hold registered locks if and only if graph/ledger checks succeed. The validator performs no frame dereference, saved-RSP update, task/lock mutation, allocation or IRQ EOI. Supervisor slot is excluded from this ordinary-task policy. Existing D5.6 and D5.5c strict frame checks remain untouched in Phase 1.
2. A task that becomes BLOCKED in `d53_acquire()` has `current_slot==SIZE_MAX`, is absent from READY, and retains its registered waiter reference. Such a task is **never** admitted as a READY IRQ destination. The caller must transfer execution to an independently owned surviving stack from a **non-IRQ IF=0 safe point**, using an separately validated x86 context handoff, before any blocked task can be considered safely suspended. A metadata-only check cannot prove current CPU RSP or safely cause that transfer. This phase verifies the metadata contract but does not implement or claim the actual x86 blocked-stack transfer.
3. At the later QEMU integration gate, after frame geometry/canary/saved-RSP/IF/provenance proofs have been independently restored for lock-bearing workers, an isolated test bridge may consume the new admission. Do **not** remove existing source/destination frame checks, modify D5.3 in ISR, force-release owner locks, resurrect stale generations, or clean live stacks from IRQ. Malformed graph or orphaned owner must fail closed before M10-A handoff. The IRQ and non-IRQ exclusion design remains pending review.

## Acceptance and alternatives

Reject stale generation, incorrect state, missing/quarantined/overlapping stack, registered BLOCKED waiter as target, forged lock/wait counters, wrong lock owner, wrong waiter generation, terminated/orphaned owner, invalid IF/IRQ witness and global graph corruption **without mutation** to any input or output. Positive low-priority owner/high-priority waiter donation and release/wake must hold exact M10-A identities and ledger equality; run hosted GCC ASan/UBSan and inherited D5.7 regressions. Normal unflagged executable must remain byte-identical to accepted D4. Keep change on independent branch/draft PR; no physical hardware, LAN, OPNsense, owner ISO replacement or production editor changes.

Alternatives rejected: allowing any nonzero count merely because a resource entry exists, silently setting `owned_locks=0`, deriving task readiness from lock graph instead of M10-A, or synthesizing an IRQ return to a BLOCKED stack. Tradeoff: full graph scan is bounded but expensive, so this is a correctness admission proof, not yet a measured or approved 100Hz production ISR algorithm.

## Change history

2026-11-14: documented blocked-stack and lock-owned frame safety contract before implementation.

## Existing D5.4 execution handoff finding (read-only audit, 2026-11-14)

The earlier cooperative QEMU fixture src/kernel/m10-d54-guest-smoke.c demonstrates actual x86 worker/dispatcher switches and BLOCKED waiter continuations, but high() and medium() call irq_restore(flags) after d53_acquire transitions RUNNING to BLOCKED and before x86_task_switch moves off that stack. This was tolerable in the isolated cooperative fixture, but is not a safe proof under timer-driven preemption. The next isolated hardware test must retain IF=0 across the authoritative BLOCKED transition and actual switch to surviving boot/supervisor context, independently sample CPU RSP on the surviving stack, then restore original IF at the proper resumed boundaries. No normal D5.4 code behavior may change.

## D5.8b Phase 1b separately flagged x86 switch witness

Under a separate diagnostic-only build flag, reuse the established D5.4 worker stack and dispatcher, but preserve IF=0 from high/medium D5.3 BLOCKED transition through the real x86_task_switch to surviving bootstrap context. On the surviving bootstrap stack, before restoring IF, sample real CPU RFLAGS and RSP, prove IF is still zero, RSP is outside all registered worker stack intervals, M10-A has no current RUNNING task and the exact blocked generation remains registered with the D5.3 waiter graph. Restore the boot dispatcher original IF only after checking the witness; when the waiter is legitimately awakened and resumes its saved continuation, restore its own saved IF separately. The worker must not execute its continuation while BLOCKED. Test flag builds only; no timer-driven mutex preemption or D5.6 frame-check relaxation.
