# SpiffOS Design Principles — v0.1
Status: PROPOSED BASELINE DOCUMENT, reflecting accepted project principles. Date: 2026-10-20.

1. **Understandability first.** One knowledgeable developer should be able to reason about and maintain the system. Favor small modules, exactly one authoritative owner for each stateful subsystem, and documented interfaces (ADR-007).
2. **Immediate computing.** Boot rapidly into an interactive BASIC prompt; retain cursor-addressable screen command entry, without a conventional desktop.
3. **Safety by construction.** User BASIC executes in ring 3, privileged operations via checked interfaces. Priority is not authority.
4. **One consistent model.** One kernel-managed message engine handles direct process messages, request/reply and named event subscriptions (ADR-007).
5. **Predictable concurrency.** Preemptive tasks, bounded resources, blocking waits, nonrecursive BASIC event handlers at safe points.
6. **Nine independent environments.** Workspaces virtualize presentation and input, not necessarily task count. The exact program/workspace relationship is open.
7. **Portable interfaces, modest implementations.** Hardware abstraction allows new drivers without assuming identical laptops; implement only justified complexity.
8. **Accessible multimedia.** Simple BASIC APIs for sprites, graphics, music and effects; deeper documented virtual device interfaces for education.
9. **Storage that survives interruption.** A power cut must not routinely make the OS unbootable; unsaved writes are not guaranteed.
10. **Networking as a foundation.** Wired TCP/IP and BASIC sockets belong in the architecture, not as a retrofit.
11. **Documentation is a gate.** Requirements, contracts and acceptance criteria are approved before code; no silent changes to accepted decisions.
12. **Test before hardware.** QEMU and automated conformance, isolation, fault and crash tests precede physical deployment.
13. **Avoid historical hardware lock-in.** Retain helpful C64 BASIC concepts but do not emulate VIC-II, SID or machine-language binaries.
14. **Human-directed, agent-implemented.** AI agents may investigate and propose; no merge, deployment or material architecture deviation without explicit owner approval.

## Decision test
For each new feature ask: Is it understandable? Does it preserve the BASIC-first experience? Is it safe under faults and power loss? Can another developer implement its documented interface? Can it be tested automatically? If not, document the trade-off and seek approval.

## Status discipline
ACCEPTED = explicitly owner-approved; PROPOSED = recommendation awaiting approval; OPEN = unresolved; DEFERRED = deliberately later; REJECTED = considered and declined. A proposed implementation detail is never promoted to accepted by implication.
