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

**Integration caveat:** The D5.3 sidecar assumes exclusive control over registered lock/wait counts for its participating task registry. The D5.2d legacy fixture cannot concurrently operate on the same registry; doing so would defeat its counter equality proof. A shared object-identity ledger or a single canonical mutex provider must be established before general production integration.


## Implementation and validation record — 2026-11-13

Created draft stacked PR #76, branch `feat/m10-d53-bounded-sync` based on PR #75 and earlier lifecycle branches. Source `src/kernel/kernel-sync-d53.[ch]` contains a four-ranked-lock, 15-ordinary-task sidecar; every participant is a complete generation-tagged M10-A handle. It uses authoritative `kernel_task_block`/`kernel_task_wake` and D5.2b reference registration; maximum fourteen waiters, highest effective-priority wake with FIFO tie-break, transitive fixed-point donation, no recursive/out-of-rank acquire, explicit cancel-before-termination and read-only orphan detection. No memory allocations, interrupt actions or second scheduler. Existing normal kernel and D5.2d cooperative QEMU guest modules are unchanged.

Local Forge QWEN `qwen3-coder:30b` was invoked first for the isolated rank comparison helper; it completed in **32.00 seconds**. Coordinator reviewed and integrated an equivalent helper with an additional invalid-high-bit guard, authored the bounded sidecar and tests. No Codex fallback.

Isolated Forge validation from `/tmp/spiffos-d53-20261113`:

- `make HOST_CC=gcc test-m10-d53-sync`: **PASS**, GCC ASan/UBSan and `-Werror`; marker `SPIFFOS_M10_D53_MULTIWAITER_OK`, including a full fourteen-waiter/one-owner capacity run, highest-priority owner handoff, transitive donation chain, reversed-rank rejection without mutation, exact waiter cancellation, owner-death orphan lock and **20,000** generational reuse cycles.
- Full `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine make HOST_CC=gcc test-m10-d53-acceptance`: **exit 0 in 266.40 seconds**, log `/tmp/spiffos-d53-acceptance.log`; markers `SPIFFOS_M10_D53_ACCEPTANCE_OK`, D5.2d, D5.2c, D5.2b, D5.2a, D5.1, D4 complete/checkpoint, D3 and D2 present. Inherited real QEMU boots at 128, 256, 512 MiB, editor/keyboard, IRQ0, #UD/#DF and APIC/MMIO positive/negative scenarios passed.
- Ordinary debug-stripped kernel ELF SHA-256: `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`, identical to established D4 normal executable baseline. Disposable ISO SHA-256: `779697dfea5f28d265a755c91c24fd9faadd3629403b665ea6851f6452da3442`, not an accepted owner release or byte-for-byte provenance claim.
- Owner normal Forge checkout inspected: no modified files. All QEMU commands used isolated OVMF variables, `-net none`, no physical drives or release ISO replacement.

Limitations: This verifies multiwaiter **hosted policy** only; the real QEMU cooperative worker handoff belongs to the inherited D5.2d **single-waiter** test. Donated effective priority is computed, not consumed by the M10-A FIFO scheduler. No production priority-aware scheduler, real multiwaiter QEMU execution, cancellation-after-owner-death recovery, ISR-safe lock, Ring 3 or general editor preemption. Exact final PR-head GitHub CI and owner merge/deployment remain separate gates.

