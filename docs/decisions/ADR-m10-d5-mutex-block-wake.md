# ADR M10-D5.2d — Synchronized block/wake on authoritative task model

Status: OWNER-AUTHORIZED ISOLATED IMPLEMENTATION, not approval to enable production preemption. Stacked on D5.2c PR #74, D5.2b PR #73, D5.2a PR #72 and D5.1 PR #71. Read AGENTS.md, accepted/open decisions, architecture overview, ADR-m10-task-scheduler-foundation.md, ADR-m10-d4-priority-supervisor-proof.md, ADR-m10-d5-production-task-model.md, ADR-m10-d5-resource-ownership.md, ADR-m10-d5-supervisor-cleanup.md and DEVELOPERS_GUIDE.

## Problem

D5.2c's one-owner/one-waiter synchronization proof changes ledger counts but never blocks a RUNNING waiter or wakes it. M10-A already has authoritative `kernel_task_block()` (RUNNING -> BLOCKED) and `kernel_task_wake()` (BLOCKED -> RUNNABLE), yet no atomic lock-to-scheduler integration. D4's priority inheritance controller is diagnostic-only and is not a production task manager.

## Decision

For this isolated checkpoint, add an **internal one-owner, one-waiter kernel mutex** backed only by M10-A handles, D5.2b resource ledger, and the preexisting M10-A block/wake transitions. Preserve the M10-A public ABI and its single CPU FIFO policy. A current RUNNING waiter trying to acquire a held mutex registers one waiter reference, blocks via `kernel_task_block`, and must yield its actual CPU context to the dispatcher before any other task runs. A current RUNNING owner releases, registers ownership for the blocked waiter, drops its waiter reference, wakes exactly that handle into the ready FIFO, and releases the former owner's registered ownership. No instant context switch or IRQ-entry actions. A woken task resumes from its saved context and sees the successful lock transfer. Waking must not occur if an owner cannot atomically complete bookkeeping; failure must leave all metadata and references unchanged.

The mutex cannot be recursively acquired, have more than one waiter, or have its own scheduler. References are generation tagged, lock state is bounded and caller-owned. Preflight the entire task table, resource registry, lock state, exact running owner/caller, counts and task states **before mutations**. All operations are one-CPU IF=0 critical sections; an interrupted or arbitrary task must not operate the lock. Unlock with a nonblocked or terminated waiter fails closed and retains original ownership. Only a dispatcher that actually saved the blocked task's context may next dispatch; the metadata transition is not proof of a real x86 context switch. Mutex destruction, dead owner cancellation, multiple locks/waiters, priority inheritance and general lock graph are deferred.

## Validation

- Hosted GCC ASan/UBSan: uncontended acquire/release, held-mutex contention blocks real M10-A RUNNING task, exact wake and transfer via FIFO, blocked waiter cannot dispatch, stale-generation and wrong owner denial, recursive and second-waiter denial, owner death and waiter death freeze without invented cleanup, duplicate wake rejection, no mutation on invalid release, 20,000 cycles of reuse, and preservation of supervisor reservation.
- Isolated QEMU proof: if adding a new execution guest integration, prove actual cooperative transfer to surviving boot dispatcher and preserved execution stack. Never treat metadata-only blocking as sufficient to run a second task concurrently.
- Run full inherited D5.2c/D4/M9 acceptance, default unflagged kernel hash comparison and final exact-head GitHub Actions.
- No merge, accepted ISO overwrite, production editor preemption, network/OPNsense or physical disk changes. Owner approval required for release.

## Rejected alternatives

Replacing M10-A with D4 controller duplicates lifecycle authority. Blocking via a helper that only increments a counter permits dispatching a supposedly waiting task. Automatically clearing lock owners when terminated can corrupt other tasks. Moving mutex operations into IRQ0 permits unsafe stack and allocator reentrancy.

## Change history

2026-11-13: owner selected D5.2d as next milestone. Documentation-first stacked branch. Test evidence recorded after execution.


## Verified real M10-B execution extension

The existing QEMU M10-B fixture now includes a four-dispatch mutex
sequence **before** its inherited 1,026 context-transfer churn. A
separately stacked owner acquires and voluntarily switches to the
bootstrap dispatcher; the other worker contends and changes from
RUNNING to BLOCKED before saving its actual x86 execution context.
The surviving dispatcher selects the owner (the blocked waiter is absent
from FIFO), the owner releases and transfers ownership/wakes the exact
generation-tagged waiter, and the waiter resumes at its saved instruction
following the suspended `x86_task_switch`, releases the mutex and yields.
The guest checks the exact four stages, kernel task/resource invariants,
waiter state transitions and zero outstanding references, then retains
the original round-robin, editor and IRQ0 tests.

Real QEMU `make HOST_CC=gcc test-m10-b-guest` **PASS** at 128, 256 and
512 MiB in disposable source
`/tmp/spiffos-d52d-real-switch-20261113`. This proves a *cooperative*
kernel-mode execution handoff for this one-owner/one-waiter internal
mutex, not IRQ-driven preemptive mutex safety, Ring 3, general priority
inheritance or multiple-waiter cancellation. The complete inherited
suite and final SHA are separate gates recorded below after execution.


## Final isolated implementation and regression ledger — 2026-11-13

Branch `feat/m10-d5-mutex-block-wake`, draft stacked PR #75, based on
D5.2c PR #74, D5.2b #73, D5.2a #72 and D5.1 #71.

- New internal `src/kernel/kernel-task-mutex.[ch]`: full generation-tagged
  one-owner/one-waiter M10-A mutex with lock/wait owner references, exact
  RUNNING→BLOCKED scheduler transition, FIFO wake/transfer, and fail-closed
  rejection of stale, recursive, wrong owner, terminal waiter and owner-death
  operations. One boot CPU, caller-held IF=0; no memory allocation or IRQ
  entrypoint. No ordinary kernel linkage.
- New `tests/m10-d5-mutex-block-wake-test.c`: GCC ASan/UBSan hosted
  20,000 contested block/wake/transfer/terminated-and-reaped cycles, explicit
  generation-safe saved-ref cleanup, invalid-call nonmutation and terminated
  owner/waiter negatives. `make HOST_CC=gcc
  test-m10-d5-mutex-block-wake`: **PASS** marker
  `SPIFFOS_M10_D5_MUTEX_BLOCK_WAKE_OK`.
- Real M10-B diagnostic fixture now proves **four actual cooperative CPU
  stack transfers** including blocked-waiter switch-away and subsequent
  saved-C-continuation resume after owner wakes and transfers mutex:
  `make HOST_CC=gcc test-m10-b-guest` **PASS** 128/256/512 MiB.
  Subsequent original 1,026 round-robin transfers and off-stack task
  cleanup still pass. The normal editor remains nonpreemptive.
- Local QWEN `qwen3-coder:30b` completed a bounded full-generation handle
  equality suggestion in 28.55 seconds. Coordinator reviewed/integrated
  equivalent logic and authored integration/tests. Initial `-Werror`
  precedence warning was fixed by explicit grouping before sanitizer
  acceptance. Codex not used.
- Full inherited Forge regression **exit 0**, runtime **267.01 seconds**:
  `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory
  GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine
  make HOST_CC=gcc test-m10-d5-mutex-acceptance` from
  `/tmp/spiffos-d52d-real-switch-20261113`, final log
  `/tmp/spiffos-d52d-final-acceptance.log`. Includes
  `SPIFFOS_M10_D5_2D_ACCEPTANCE_OK`,
  `SPIFFOS_M10_D5_2C_ACCEPTANCE_OK`,
  `SPIFFOS_M10_D5_2B_ACCEPTANCE_OK`,
  `SPIFFOS_M10_D5_2A_ACCEPTANCE_OK`,
  `SPIFFOS_M10_D5_1_ACCEPTANCE_OK`, D4/D3/D2 markers, and M9
  keyboard/editor/workspace, 100Hz IRQ0, exceptions #UD/#DF, MMIO/APIC,
  positive and negative QEMU scenarios.
- Normal unflagged stripped kernel ELF SHA-256:
  `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`,
  identical to D4 ordinary executable baseline. The disposable normal ISO
  SHA-256 is `6f12d08f0f0ef047e6c83879496afeb9c1c41746e5dc7e3f9b0539b9db586922`;
  not an owner deployment or assertion of byte-identical ISO provenance.
- All testing used disposable Forge source, isolated OVMF, QEMU
  `-net none`, pinned Limine read-only dependency, and no physical disks.
  Owner's main Forge checkout was inspected and remained unchanged.

Remaining gating: exact final-head GitHub Actions result, owner review
before merge/deployment, multiwaiter contention/cancellation, inheritance,
nested lock ordering, all-or-nothing allocator rollback and generalized
production scheduler reentrancy. The cooperative proof does not authorize
ordinary editor IRQ preemption, Ring 3 or physical media.
