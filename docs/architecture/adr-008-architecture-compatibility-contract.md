# ADR-008 — Architecture Compatibility Contract

Status: ACCEPTED design policy (2026-10-20); implementation details OPEN
Scope: Documentation only; no implementation authorization

## Purpose
New features must preserve boot recovery, process isolation, filesystem consistency, workspace ownership and emergency Supervisor availability. The authoritative-owner rule from ADR-007 applies throughout. These safeguards reconcile ADR-001 through ADR-007; they do not replace their other accepted requirements.

## C1 — Boot rollback and filesystem format compatibility
- SpiffFS on-disk formats are explicitly versioned.
- Before activating a new OS slot, the update manager verifies that both the candidate and rollback OS can safely access the current user-data format.
- No silent incompatible data-format migration. Any incompatible migration requires an explicitly approved procedure with backup, validation, and recovery/rollback strategy. Where rollback cannot safely access migrated data, do not claim automatic rollback is safe.
- A/B OS slots, EFI partition and user-data SpiffFS remain independently managed.
- Interrupted update or migration must not silently destroy the last committed user data.
- Tests: upgrade/rollback version matrix, interrupted metadata updates, incompatible migration rejection and recovery.

## C2 — Filesystem concurrency and lost-update prevention
- SpiffFS is the sole authority for namespace, file locks, atomic commits, versions and recovery.
- Provide enforced exclusive update locks and atomic file transactions through one service.
- Locking and atomicity are distinct: a lock serializes cooperating access; a durable commit preserves the last committed state through interruption.
- Detect conflicting stale updates (e.g., compare expected file version/generation) rather than silently overwrite another writer's newer committed content.
- Automatically release process-owned locks after termination. Define lock acquisition, timeouts, failure behavior and cleanup ordering before implementation.
- Preserve SpiffFS recovery/version budgets and crash consistency under concurrent cleanup and restoration.
- Tests: two-writer conflicts, crash with lock held, power failure during save, version restoration while writer active.

## C3 — Workspace foreground ownership
- Each of the nine workspaces has one designated foreground application for focused keyboard input and its main presentation surface.
- Other processes in the same workspace may run concurrently, maintain separate virtual surfaces and publish events without seizing focus.
- The Workspace Manager is the sole authority for membership, focus and presentation composition; hardware display/input drivers retain hardware ownership.
- Cross-workspace input and output require explicit permissions. Switching workspaces changes physical presentation ownership without suspending background processes.
- The model permits future windows, split screens or additional presentation surfaces without changing process isolation.
- Tests: concurrent applications, focus transfer, background execution, unauthorized input capture and workspace switching.

## C4 — Emergency Supervisor availability
- Reserve bounded CPU scheduling capacity, memory, and emergency keyboard/display takeover paths for Supervisor recovery independent of BASIC interpreter responsiveness.
- Ctrl+Alt+Delete must be recognized outside ordinary BASIC event handling; ordinary processes cannot consume or intercept the emergency sequence.
- A runaway, blocked, memory-exhausting or faulting BASIC process must not prevent Supervisor entry and safe termination.
- Privileged actions remain restricted to the Supervisor; Supervisor is not a tenth workspace.
- Tests: infinite loop, memory exhaustion, queue flood, blocked I/O, graphics overload and task crash.

## Compatibility gate for future features
Before accepting a new feature, check its effect on:
1. A/B boot and user-data format rollback safety.
2. Ring 3 memory and privilege isolation, including native code and virtual PEEK/POKE.
3. SpiffFS locking, transactions, versions and crash consistency.
4. Workspace focus, virtual presentation and background process continuity.
5. Reserved Supervisor recovery resources and input/display access.
6. Unified Message Manager contracts and authoritative subsystem ownership.

A failed check requires explicit design reconciliation and a new ADR before implementation.

## Known limitations and open implementation details
A modular monolithic kernel remains the v1 choice. Ring 3 protects the kernel from ordinary application faults but cannot contain arbitrary faults in Ring 0 drivers. Maintain small modular driver interfaces, defensive validation, fault-injection testing and recovery diagnostics rather than introducing a microkernel in v1.

OPEN: exact SpiffFS compatibility matrix and migration mechanics; lock/transaction API; stale-write version tokens; workspace compositor and focus policy; Supervisor resource budgets and failure behavior; driver fault handling. This ADR sets policy, not implementation specifics.
