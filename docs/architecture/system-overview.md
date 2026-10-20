# SpiffOS System Architecture — Baseline v0.1
Status: PROPOSED DOCUMENT; architecture decisions below retain their individually recorded statuses. Date: 2026-10-20.

## Mission and boundaries
SpiffOS is a new native x86-64 operating system inspired by the simplicity and immediacy of early personal computers. It is neither an emulator, Linux distribution, nor compatibility layer. Motto: **Simple enough to understand. Powerful enough to create.**

## Accepted system model
UEFI boots a standalone SpiffOS kernel into an interactive, unnumbered structured BASIC environment. No conventional desktop. QEMU on Forge precedes physical laptop validation. The kernel runs in ring 0; BASIC applications in ring 3, isolated from kernel memory and one another. A timer-driven preemptive scheduler supports Background, Normal, Interactive, and Time-sensitive priorities; priority never grants privilege.

Nine independent virtual BASIC workspaces retain program, cursor, screen, and execution state. Ctrl+Alt+1–9 select workspaces, Ctrl+Alt+0 shows overview, Ctrl+Alt+Delete enters a privileged supervisor rather than ordinarily rebooting. Only the foreground workspace owns physical display and audible output; background playback advances silently.

A unified kernel Message Manager provides process inboxes, direct single-recipient messages, correlated request/reply and named event subscriptions with per-subscriber fan-out; subscriptions receive future events only, with polling and safe-point event handlers. Queue limits, overflow policy and exact API syntax remain open; payload families, immutable delivery, kernel permissions and safety requirements are recorded in ADR-006/007. BASIC supports named SUB/FUNCTION, local variables, structured control flow, files, graphics, sound, networking, processes and messages. Interactive screen entry works away from a fixed bottom prompt; EDIT opens a two-pane source/command environment, Tab switches panes, EXIT leaves editor. Pane focus must be visibly explicit, with a Turbo Pascal-inspired but unapproved menu design.

Logical graphics resolution is independent of physical hardware. Documented virtual PEEK/POKE device access must not compromise ring-3 isolation. Sound supports simultaneous music/effects. Hardware uses modular drivers. Storage supports folders and power-loss resilience without promising survival of unsaved writes. Wired TCP/IP (IPv4, TCP, UDP, DNS, client/server APIs) is a first-class capability; Wi-Fi is later.

## Proposed component boundaries (not approved interfaces)
UEFI loader → kernel startup/interrupts/memory/scheduler/syscalls → task and message services → BASIC runtime and supervisory UI. Drivers for display/input/storage/audio/Ethernet serve virtual workspace devices. Filesystem and network services expose controlled APIs. This diagram is conceptual; whether services/drivers execute in kernel or user space is OPEN.

## Requirements and verification
R-BOOT: boot directly to BASIC; QEMU boot-time test.
R-ISO: ring-3 cannot write ring-0 or other process memory; fault injection.
R-SCHED: preemption and four priorities; CPU contention tests.
R-SUP: Ctrl+Alt+Delete reachable during runaway BASIC; stress tests.
R-IPC: subscription-time fan-out and bounded delivery; queue tests.
R-WS: nine retained virtual screens and background task progress; switching tests.
R-AUDIO: background muted with advancing timeline; transition tests.
R-STORAGE: sudden power loss must not routinely prevent boot; crash simulation.
R-NET: BASIC network client/server use cases; integration tests.
R-DOC: every BASIC command specifies syntax, parameters, behavior, errors and examples; documentation lint/review.

## Dependency and risk notes
A ring-3 BASIC prompt requires boot, paging, interrupts, user-mode transition and syscall boundaries. Workspace isolation requires process ownership and virtual display/input. Event callbacks require interpreter safe points. PEEK/POKE requires explicit virtual address semantics. Emergency supervisor must reserve execution/input/display resources. Power-loss-safe filesystem design must precede persistent writable storage. Networking depends on drivers, timers, memory buffers and scheduling.

## Preliminary stages (not approved schedule)
Architecture approval; loader/kernel; protection/interrupts; scheduler; processes/workspaces; BASIC; screen/editor; IPC; graphics/audio; storage; drivers; networking; physical validation. Reconcile dependencies before approving a delivery schedule.

## Governance
Documentation first; code second. All implementation by AI agents, with owner approval of architecture and merges/deployments. Feature branches, PRs, validation, ADRs, and change history required. This baseline is for review and does not authorize implementation.
