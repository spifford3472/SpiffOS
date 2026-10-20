# Open Decisions Register — v0.1
Date: 2026-10-20. All items OPEN unless explicitly marked PROPOSED or DEFERRED. No implementation authorization implied.

| ID | Priority | Decision | Dependencies / acceptance question |
|---|---|---|---|
| O-001 | P0 | Loader strategy, kernel image, binary separation | UEFI exit, fast boot, recovery, debuggability |
| O-002 | P0 | C toolchain, freestanding runtime, assembly ABI | Reproducible QEMU builds; minimal dependencies; language PROPOSED |
| O-003 | P0 | Paging layout, process address spaces, allocators | Ring-3 isolation, fault handling, limits |
| O-004 | P0 | Interrupt/syscall ABI, scheduler timer/quantum | Preemption and supervisor responsiveness |
| O-005 | P0 | Kernel vs user-space driver/service boundaries | Complexity, fault containment, protected APIs |
| O-006 | P0 | Task/workspace ownership model | Nine workspaces vs number of runnable programs |
| O-007 | P0 | Supervisor input/display takeover and reserved resources | Works during BASIC runaway |
| O-008 | P0 | Filesystem and immutable/read-only boot partition strategy | Crash consistency, atomic updates, recovery |
| O-009 | P0 | Virtual PEEK/POKE graphics/sound address contract | Educational low-level access without violating isolation |
| O-010 | P1 | Message payload types, permissions, limits, ordering | Bounded per-subscriber fan-out; overflow diagnostics |
| O-011 | P1 | BASIC grammar, typing, scoping, errors and modules | Parser and conformance tests |
| O-012 | P1 | Interpreter safe points, event-handler blocking/reentrancy | Event-driven safety and responsiveness |
| O-013 | P1 | Input focus vs broadcast keyboard subscriptions | Multiple subscribers without unintended keystrokes |
| O-014 | P1 | Logical resolution, text modes, sprites and frame sync | Virtual buffers and device drivers |
| O-015 | P1 | Sound voices, mixer, background timing | Music/effects with foreground-only output |
| O-016 | P1 | Hardware inventory, minimum CPU/RAM, USB and driver ABI | Laptop and QEMU targets |
| O-017 | P1 | IPv4 stack, DHCP, TCP/UDP/DNS, sockets and async events | Ethernet, timers, buffering, security |
| O-018 | P1 | BASIC file commands and persistence/error contracts | Chosen filesystem semantics |
| O-019 | P2 | Editor navigation, undo, source management and output overflow | Unnumbered BASIC and two-pane editor |
| O-020 | P2 | Turbo Pascal menu research and adapted keyboard scheme | No menu design approved |
| O-021 | P2 | Workspace overview, default programs, resource limits | Task ownership and virtual screen |
| O-022 | P2 | Driver recovery, watchdog, diagnostic boot | Supervisor and hardware failure cases |
| O-023 | P2 | Release policy, compatibility and native applications | ABI and language stability |
| O-024 | DEFERRED | Wi-Fi, IPv6, optional Python/MicroPython, multi-monitor, legacy BIOS | Reassess after foundational releases |

## Potential tensions (not contradictions resolved by fiat)
- UEFI fast boot vs hardware discovery and recovery: define bounded initialization and diagnostics.
- Ring-3 isolation vs raw PEEK/POKE: choose virtual device registers rather than physical kernel addresses, subject to approval.
- Multiple keyboard subscribers vs focus: separate raw system events from focused application input.
- High-priority tasks vs supervisor: reserve scheduler/input/display paths and define preemption rules.
- Background audio advancing vs muted hardware output: separate logical playback timeline from physical mixer.
- Nine workspaces vs multiple tasks: explicitly define workspace/task relationship.
- Documentation-first sequencing vs early QEMU experiments: exploratory research must not become implementation before design gate.

## Approval gates
G1: architecture and decision inventory; G2: boot/kernel/memory/scheduling interfaces; G3: BASIC/IPC/workspace/editor contracts; G4: graphics/audio/storage/network/driver contracts; G5: authorization for code. Owner explicitly approves each gate. Review this register at every PR.
