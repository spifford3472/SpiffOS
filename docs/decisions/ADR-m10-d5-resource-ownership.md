# ADR M10-D5.2b — Task resource ownership and safe termination gate

Status: OWNER-DIRECTED ISOLATED IMPLEMENTATION, not production scheduling approval. Stacked on D5.2a PR #72 and D5.1 PR #71.

References: AGENTS.md; docs/decisions/accepted.md; docs/decisions/open.md; docs/architecture/system-overview.md; ADR-m10-task-scheduler-foundation.md; ADR-m10-d5-production-task-model.md; ADR-m10-d5-execution-aware-reaper.md; docs/DEVELOPERS_GUIDE.md.

## Decision

Extend the existing M10-A/D5.1 task identity and D5.2a execution-aware cleanup **without introducing another scheduler**. For this checkpoint use a bounded fixed-capacity `kernel_task_resources` registry indexed by the 16 authoritative task slots and bound to the full (slot, generation) identity. Only the trusted dispatcher/owner may register a stack's complete immutable bounds/saved context and an exact owner token. Never trust a caller-supplied stack range during release; compare the supplied context against the registered owner record, or, preferably, release directly from a copied registered binding. Do not store or dereference a pointer into task-owned stack backing after release.

Introduce explicit bounded lock-ownership and waiter-registration counts as an **interlock** for future synchronization modules, not as an independent lock manager or a claim that locks already register there. Count changes require an exact task handle and registered generation, reject overflow/underflow, and are allowed only before TERMINATED. A task with remaining lock or waiter registrations may become logically TERMINATED but must **not be reclaimed**. Release eligibility also requires descriptor TERMINATED and a valid installed context; otherwise fail closed. The supervisor/dispatcher runs cleanup only with exclusive IF=0, outside task stack, using the existing D5.2a reaper. Never release backing while a descriptor has live synchronization references. A memory-owner failure must preserve an unreclaimed descriptor and the binding, enter a terminal **quarantine** state that blocks automatic retry, and demand operator-controlled recovery; no blind repeated frees after partial release.

### Scope and limits

- One static registry of capacity 16; supervisor reservation remains authoritative in the task table. No dynamic allocations, DMA, threads, new syscalls, scheduler or IRQ path.
- An owner supplied stack binding is registered once per generation. Exact owner index **is not assumed to equal task slot**, although this fixture uses indices 0 and 1. Require nonzero, aligned, nonoverflowing stack range and valid saved context at retirement.
- Do not conflate this interlock with true ownership of existing production locks: all future mutex/wait APIs must register and release references through this registry before task termination is enabled outside the diagnostic.
- The existing M10-B diagnostic continues to prove real stack switch and frame ownership; integration must be test-only.
- No claim of all-or-nothing physical frame release: the current M10-B memory allocator can poison itself after a partial failure. Quarantine instead of retry is mandatory.
- If a release succeeds but metadata transition fails unexpectedly, fail closed; NEVER publish a reusable descriptor with uncertain backing ownership.

### Tests and acceptance

Hosted ASan/UBSan: stale generations, duplicate registration, missing registration, stack tampering, invalid task state, lock/wait blockers, counter overflow/underflow, unsafe active stack, refused owner release and quarantine/no retry, supervisor-only handoff, 20,000 safe retirements and invariant checking. M10-B real QEMU at 128/256/512 MiB must use registered owner metadata, reject self-free, and release from surviving boot stack. Run inherited M10-D5.2a/D5.1/D4/QEMU full gate, confirm normal unflagged kernel ELF unchanged and exact-head GitHub Actions success. Retain accepted ISO and rollback. No merges, deployments, network or physical disk changes without separate owner authorization.

### Alternatives considered

Adding a second scheduler: rejected. Trusting only D5.2a caller-provided stack pointer: rejected because a mismatched owner token could free another task's backing. Automatically releasing lock/wait registrations on task death: rejected because it can strand foreign references or violate mutex invariants. Retrying a failed release: rejected because partial release can double-free frames. Using heap-allocated registry: unnecessary for 16 bounded slots.

### Change history

2026-11-13: owner accepted proposed D5.2b development; documentation-first stacked branch. Tests and implementation outcomes to be recorded after verification.


## Verified implementation and test record — 2026-11-13

Branch `feat/m10-d5-resource-ownership-gate` is stacked on D5.2a PR
#72; opened as draft PR #73. Implemented `kernel-task-resources.[ch]`,
a 16-entry ledger bound to authoritative generation-tagged M10-A task
handles, with immutable backing and owner identity, mutable
dispatcher-saved RSP, bounded lock/wait reference interlocks and
nonretrying quarantine. The real M10-B QEMU test now registers each
stack before first dispatch, refreshes saved RSP after returning to the
boot context, rejects active-stack release and retires via its
registered exact-owner binding. No ordinary kernel image links this
new module. The open production mutex and wait APIs **do not yet**
register references, so a general production reaper is not claimed.

QWEN local `qwen3-coder:30b` completed a narrow counter-adjustment
helper suggestion in 29.56 seconds. It proposed standard `UINT_MAX`
overflow and zero underflow checks with no mutation on refusal. The
coordinator integrated equivalent reviewed checks into the single
bounded owner registry and wrote test/model/QEMU integration code;
Codex was not used. Initial hosted GCC `-Werror` caught an ambiguous
operator-precedence expression; fixed explicitly and retested. No
owner checkout permissions or Git global settings were changed.

**Forge source-isolated acceptance:**
`/tmp/spiffos-d52b-20261113` copied from the isolated D5.2a
worktree, with the D5.2b GitHub branch edits applied. Hosted command:
`make HOST_CC=gcc test-m10-d5-resource-ownership`
returned **PASS**, `SPIFFOS_M10_D5_RESOURCE_OWNERSHIP_OK`,
with ASan/UBSan and 20,000 create/register/reap cycles, stale and
duplicate owner identities, overlapping stack addresses, blocked
lock/wait cleanup, active-stack denial, ordinary-task denial, reference
counter failures, quarantine and no retry. M10-B real QEMU:
`make HOST_CC=gcc test-m10-b-guest` returned **PASS** at
128/256/512 MiB.

Full inherited regression with a process-local Git safe.directory for
the pinned read-only Limine dependency:
`make HOST_CC=gcc test-m10-d5-resource-acceptance` returned
**exit code 0 in 262.85 seconds**. Log:
`/tmp/spiffos-d52b-acceptance.log`. The markers
`SPIFFOS_M10_D5_2B_ACCEPTANCE_OK`,
`SPIFFOS_M10_D5_2A_ACCEPTANCE_OK`,
`SPIFFOS_M10_D5_1_ACCEPTANCE_OK`,
`SPIFFOS_M10_D4_COMPLETE_ACCEPTANCE_OK`,
`SPIFFOS_M10_D4_CHECKPOINT_OK`,
`SPIFFOS_M10_D3_ACCEPTANCE_OK` and
`SPIFFOS_M10_D2_ACCEPTANCE_OK` were all present, including
keyboard/editor, IRQ0, exception, APIC/MMIO, negative, and all three
QEMU memory-size regressions.

Unflagged default kernel ELF stripped-debug SHA-256:
`82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`,
matching documented D4 normal baseline. This proves executable
isolation, not byte-identical ISO provenance. The owner's accepted
ISO and rollback files were not replaced or used as development outputs.

**Unresolved acceptance:** exact final PR-head GitHub Actions CI and
owner signoff before merge/deploy. D5.2b does not resolve actual
production mutex/wait registration, partial-release recovery, deferred
quarantine remediation or supervisor's bounded cleanup queue.
