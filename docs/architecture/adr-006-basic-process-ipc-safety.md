# ADR-006 — BASIC processes, networking and safe IPC

Status: Accepted design direction; command signatures remain proposals
Date: 2026-10-20

## Process architecture
- SpiffOS v1 supports independently scheduled BASIC processes in separate protected address spaces. Lightweight shared-memory threads/tasks are deferred.
- Each process owns its variables, arrays, handles, sockets and memory; it may coexist with other processes in the same workspace.
- Processes use OS-managed **message passing**, not shared writable memory, for v1 interprocess communication.
- Support start, status, wait, graceful stop, exit status, request/reply, send and receive. Proposed `TASK.START`, `TASK.STATUS`, `TASK.WAIT`, `TASK.STOP`, `TASK.EXIT`, `TASK.SEND`, `TASK.RECEIVE`, `TASK.REQUEST`, `TASK.REPLY`; exact signatures open.
- `TASK.STOP` requests graceful shutdown. Forced termination is reserved for protected Supervisor; ordinary applications cannot kill arbitrary processes in other workspaces. Termination cleans up OS resources.
- Process handles must be opaque and generation-safe to avoid stale handles referring to new processes.

## IPC safety requirements
1. Atomic delivery of complete messages, never partial delivery.
2. FIFO ordering for messages from a given sender to a given recipient; no global order guarantee across independent senders.
3. Single-consumer delivery per queued message, with defined concurrency semantics.
4. Request/reply correlation IDs so out-of-order replies are matched correctly.
5. Blocking and nonblocking receive, configurable timeouts, and catchable failures.
6. Bounded queues and explicit backpressure/queue-full behavior.
7. Immutable message contents after send; strings, numeric values, Booleans and `BYTES` supported.
8. Waiting callers learn promptly when the destination exits; where practical detect straightforward synchronous request deadlocks.
9. Enforce process/workspace permissions in the kernel, not solely in the BASIC interpreter.
10. Race-free queue operations and safe wakeups; cleanup must not race with send/receive.

Messages and handles do not by themselves make multi-step updates atomic. Define a single writer, locking or transactions for shared resources.

## Filesystem concurrency
- Enforced file locks, including an exclusive-update option, with automatic release on process exit/crash.
- Lock and save semantics must coexist with SpiffFS atomic commits, version history and cleanup.
- Specify lock acquisition order, lock timeouts and error behavior before implementation.

## Networking
- TCP/IP networking exposes byte-preserving `BYTES` send/receive operations; text conversion is explicit.
- Provide simple blocking operations with configurable timeouts and advanced nonblocking readiness handling.
- Blocking waits suspend only the calling task, never the entire OS, UI, audio or other workspaces.
- TCP is a stream: receive may return fewer bytes than requested, and send may require handling partial progress. Higher-level helpers must define framing explicitly.
- Network errors are catchable through `TRY/CATCH`; socket handles and buffers remain process-isolated.

## Open questions
Message schema and serialization; queue quotas and overflow policy; cancellation semantics; IPC authorization and naming; deadlock detection scope; delivery acknowledgement guarantees; lock API and atomic multi-file transactions; exact BASIC syntax and errors; socket event API; lifecycle hooks and Supervisor escalation.

No code or deployment changes authorized by this ADR.
