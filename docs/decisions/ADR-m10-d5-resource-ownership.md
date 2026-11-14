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
