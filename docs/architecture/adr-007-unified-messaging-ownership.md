# ADR-007 — Unified Message Manager and authoritative subsystem ownership

Status: ACCEPTED architecture direction (2026-10-20); detailed APIs OPEN
Scope: Documentation only

## Decision 1 — One unified IPC architecture

SpiffOS uses **one kernel-managed message-delivery engine** for interprocess requests, replies, published events and system notifications. Do not implement separate, competing channel and process-queue systems.

- Each process owns a protected inbox managed by the kernel.
- Direct messages route to one addressed recipient; a message is consumed once.
- Named event subscriptions are routing rules: publishing an event produces a logically independent immutable delivery for each eligible subscriber. Subscriptions receive future publications only.
- Both delivery modes use the same queue primitives, accounting, authorization, wakeups and error handling. A broadcast is **not** one message competed for by subscribers.
- Preserve per-sender-to-recipient FIFO ordering. Do not promise a global order across unrelated senders.
- Delivery is atomic and immutable; support numbers, Booleans, strings and BYTES. Efficient shared backing is allowed only if no recipient can mutate another process's payload.
- Bounded per-process queues, defined overflow/backpressure, cancellation, timeout, process-exit notification and generation-safe handles are required. Precise limits/policies are OPEN.
- Request/reply uses correlation identifiers and does not accidentally consume unrelated events or responses.
- Event handlers execute at safe interpreter/runtime points without unbounded recursion; a blocked handler must not stall the kernel or unrelated processes. Compiled BASIC follows the same contract.
- Keyboard/input events are focus-controlled by default; privileged/system broadcasts require explicit permissions.
- Suggested BASIC namespace: MESSAGE.SEND, MESSAGE.RECEIVE, MESSAGE.REQUEST, MESSAGE.REPLY, MESSAGE.SUBSCRIBE, MESSAGE.PUBLISH. Syntax is illustrative, not finalized.
- TASK.START/STOP/STATUS/WAIT/EXIT remain process-lifecycle operations; older TASK.SEND/RECEIVE/REQUEST proposals are superseded as **preferred naming**, not removed as capabilities. Alias decisions OPEN.

## Decision 2 — One authoritative owner per subsystem

Every stateful subsystem has exactly one authoritative owner responsible for consistency and permissions; other components interact through documented interfaces. This does **not** require every low-level kernel operation or high-throughput data transfer to be encoded as a message.

- **IPC manager** owns message queues, routing, subscription records and message accounting.
- **Process/task manager** owns process lifecycle, handles, scheduling integration and termination cleanup.
- **Workspace manager** owns workspace membership, focus and access to virtual presentation surfaces; display driver owns physical display hardware. Each workspace has a defined presentation policy for its multiple processes.
- **Audio service** owns physical output arbitration and foreground-only audible routing; virtual playback state continues for background workspaces.
- **SpiffFS** owns namespace, locking, transactions/atomic commits, versioning, deletion and recovery accounting; processes never directly manipulate its metadata.
- **Network stack/socket service** owns socket state and TCP/IP protocol consistency; readiness/completion events may be published through IPC.
- **Supervisor** owns privileged recovery actions and maintains reserved CPU, memory, input and display paths independent of the BASIC interpreter.
- **Loader/update manager** owns A/B boot metadata and pending-update rollback policy, separate from SpiffFS user data.

The authoritative owner may be a kernel component or a privileged service as specified by the existing kernel architecture. The rule forbids **duplicate sources of truth**, not internal modularity.

## Reconciliation and compatibility

1. ADR-001's named channel IPC and ADR-006's direct process IPC are two routing modes of the **same engine**, not independent implementations.
2. Design principles' 'named channels primary' wording is superseded by **unified messaging**, retaining named subscriptions as a first-class capability.
3. Background programs continue running and rendering to virtual workspace state; only authorized workspace composition reaches physical display/audio.
4. `SYS` and `BLOAD` remain constrained by process isolation and W^X. Message passing does not grant physical memory access.
5. File locks coordinate concurrent writers but cannot replace SpiffFS atomic commit and versioning; locking/commit ordering must be specified.
6. Blocking MESSAGE/FILE/TCP operations suspend only the calling process, never the entire system or Supervisor.
7. A/B rollback does not automatically reverse user-data format changes; schema migration compatibility is a separate gate.

## Acceptance tests before implementation

- Direct message delivered once; publish delivered to each subscriber independently; late subscribers do not receive historical events.
- Queue-full, timeout, cancellation and recipient-exit tests; concurrent send/receive with process teardown.
- Request/reply correlation with out-of-order replies and unrelated incoming events.
- Cross-workspace permissions and focused keyboard input; no unprivileged supervisor impersonation.
- Multiple processes in one workspace cannot bypass presentation ownership.
- Runaway and memory-exhausting BASIC process cannot starve Supervisor.
- File-lock/crash/atomic-save/recovery races do not corrupt last committed data.
- Interpreted and compiled BASIC observe identical messaging semantics.

## Open details

Queue quota and backpressure policy; event names and subscription lifecycle; event handler reentrancy; ordering between direct and published messages; service ownership failure/restart semantics; workspace compositor contract; multimedia streaming bypass paths; kernel/user-service API boundaries.

No code changes, merge or deployment are authorized by this ADR.
