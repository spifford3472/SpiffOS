# ADR M10-D5.3 — Bounded multiwaiter synchronization and priority donation

Status: owner-directed **isolated implementation and validation** on 2026-11-13. This is not a production scheduler, ABI or deployment approval. Stacked on draft PRs #75, #74, #73, #72, #71. Governing references: AGENTS.md, docs/decisions/accepted.md, docs/decisions/open.md, docs/architecture/system-overview.md, ADR-m10-task-scheduler-foundation.md, ADR-m10-d4-priority-supervisor-proof.md, ADR-m10-d5-production-task-model.md, ADR-m10-d5-resource-ownership.md, ADR-m10-d5-supervisor-cleanup.md, ADR-m10-d5-mutex-block-wake.md and docs/DEVELOPERS_GUIDE.md.

## Problem

The M10-D5.2d cooperative kernel mutex supports one owner and one waiter. The M10-A task table is the sole lifecycle authority but has no production priority field, ownership graph, deadlock detection, or recovery protocol. D4's priority controller is test-only and must not be copied or silently made authoritative.

## Scoped decision

Introduce a **separate bounded synchronization sidecar, not another task manager**, for at most four ranked mutexes and fifteen ordinary task identities. All slots are generation-keyed by authoritative M10-A handles. Each lock supports up to fourteen ordered waiting handles; any one task may wait on only one mutex. Every waiter registers a reference with the D5.2b ledger and enters M10-A BLOCKED. Only the exact RUNNING owner can unlock and transfer one registered lock reference to the highest effective priority waiter (FIFO tie-break), then wake it through M10-A. Remaining waiters stay BLOCKED with live ledger refs. No IRQ context, dynamic allocation, public ABI or implicit CPU switch. A task that blocks must yield its real x86 stack to the surviving dispatcher before another task executes.

Each lock has immutable rank 0..3. A task acquiring a lock while already holding locks must use a strictly higher rank than every lock it holds. Reject recursive acquisition, descending/equal rank, duplicate waiter, full queues, and stale identities without mutation. This avoids circular wait between compliant clients and bounds dependency depth. Supervisor slot 15 may not own or wait.

Record base priority (four bands) per generation in sidecar. Compute *effective priority* as a bounded fixed-point donation over lock wait-for edges, without mutating base priority. An owner receives the maximum effective priority of its waiters, including transitive donation along strictly ordered rank chains. The donor may be blocked; a runnable owner is not automatically scheduled because M10-A is FIFO. A separate scheduler integration gate must consume effective priorities before the system claims priority inversion avoidance. Never use diagnostic D4 as a second scheduler.

**Owner termination is fail-closed:** a lock whose owner has terminated is marked orphaned for inspection; it is not automatically unlocked, transferred, freed or retried. Blocked waiters retain references, thus the supervisor cleanup queue cannot reclaim them. Explicit privileged recovery requires a later resource integrity and lock-specific teardown design. **Waiter cancellation** may be issued only by the surviving dispatcher/supervisor while the exact waiter remains BLOCKED; it must drop the registered waiter ref, remove its wait entry, and wake that task before ordinary termination (never erase refs from a TERMINATED task). This is not the same as automatic owner-death recovery.

All mutations must be preflighted under a single-CPU IF=0 contract. The transition implementation may rely on the already validated M10-A APIs only when their preconditions have been checked. Do not promise transactional recovery after a memory-owner partial-release failure; D5.2b quarantine remains permanent.

## Alternatives and non-goals

Rejected: unbounded lists and allocation, a second task scheduler, automatic dead-owner lock release, bypassing the resource ledger, and switching the ordinary editor to preemptive execution. Production priority-aware dispatch, multiple CPUs, Ring 3, general condition variables, IRQ-safe locks, timeout semantics, and guaranteed supervisor CPU budgets remain OPEN.

## Acceptance

Hosted GCC ASan/UBSan: at least 20,000 cycles with multiple waiters, fair tie-break, priority donation/revocation (including transitive bounded chains), rank enforcement, generation-stale handles, queue capacity, cancellation-before-termination, owner death/orphan quarantine, malformed state refusal and unchanged failure snapshots. Real QEMU cooperative multiworker handoff where feasible; 128/256/512 MiB, stack canaries, editor, IRQ0 and inherited D2–D5 suites. Normal stripped ELF hash must remain unchanged, exact-head GitHub CI must pass. Code and docs in dedicated draft PR; no merge, accepted ISO overwrite, network/OPNsense or physical disk changes.

## History

2026-11-13: owner requested D5.3 implementation following D5.2d. Documentation first; recorded bounds and safety choices prior to source changes.
