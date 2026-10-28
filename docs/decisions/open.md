# Open Decisions Register — v0.1
Date: 2026-10-20. All items OPEN unless explicitly marked PROPOSED or DEFERRED. No implementation authorization implied.

| ID | Priority | Decision | Dependencies / acceptance question |
|---|---|---|---|
| O-001 | P0 | Loader strategy, kernel image, binary separation | UEFI exit, fast boot, recovery, debuggability |
| O-002 | P0 | C toolchain, freestanding runtime, assembly ABI | Reproducible QEMU builds; minimal dependencies; language PROPOSED |
| O-003 | P0 | Paging layout, process address spaces, allocators | Ring-3 isolation, fault handling, limits |
| O-004 | P0 | Interrupt/syscall ABI, scheduler timer/quantum | Preemption and supervisor responsiveness |
| O-005 | P0 | Kernel vs user-space driver/service boundaries | Complexity, fault containment, protected APIs |
| O-006 | P0 | Workspace foreground/composition API | ADR-008 settles one foreground app per workspace; surface/focus details open |
| O-007 | P0 | Supervisor reserve sizing and takeover implementation | ADR-008 requires protected CPU/memory/input/display resources |
| O-008 | P0 | SpiffFS on-disk format, compatibility matrix and migration protocol | ADR-003/004/008 establish A/B and crash-consistency policy |
| O-009 | P0 | Virtual PEEK/POKE graphics/sound address contract | Educational low-level access without violating isolation |
| O-010 | P1 | Unified Message Manager quotas, overflow, cancellation, event naming and handler semantics | ADR-007 settles one engine and both routing modes; details remain open |
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

| O-025 | P0 | File transaction and conflict-token APIs | ADR-008 requires locks, atomic commits and stale-write detection |\n\n## Potential tensions (not contradictions resolved by fiat)
- UEFI fast boot vs hardware discovery and recovery: define bounded initialization and diagnostics.
- Ring-3 isolation vs raw PEEK/POKE: choose virtual device registers rather than physical kernel addresses, subject to approval.
- Multiple keyboard subscribers vs focus: focused application input is default; privileged broadcasts require explicit authorization under ADR-007.
- High-priority tasks vs supervisor: reserve scheduler/input/display paths and define preemption rules.
- Background audio advancing vs muted hardware output: separate logical playback timeline from physical mixer.
- Nine workspaces vs multiple tasks: explicitly define workspace/task relationship.
- Documentation-first sequencing vs early QEMU experiments: exploratory research must not become implementation before design gate.

## Approval gates
G1: architecture and decision inventory; G2: boot/kernel/memory/scheduling interfaces; G3: BASIC/IPC/workspace/editor contracts; G4: graphics/audio/storage/network/driver contracts; G5: authorization for code. Owner explicitly approves each gate. Review this register at every PR.

## Status changes — 2026-10-26

- **O-001 partially resolved:** UEFI + Limine approved (ADR-011). Kernel image layout, boot protocol version pinning, recovery and binary boundaries remain OPEN.
- **O-002 partially resolved:** C with minimal assembly approved. Exact compiler/toolchain, freestanding runtime and ABI remain OPEN.
- **O-003 partially resolved:** 64-bit paging, separate user address spaces, RAM-only/no swap, reserved emergency memory and recoverable OOM approved. Exact page-table layout, allocators and reserve sizing remain OPEN.
- **O-005 partially resolved:** modular monolithic kernel and user-space BASIC/EDIT approved. Driver interface details and service placement remain OPEN.
- **O-016 partially resolved:** HP Anvilone UEFI laptop (i5-7200U, 16 GB, Intel HD 620, SATA AHCI, Realtek Ethernet) selected for first physical USB tests; full hardware support requirements remain OPEN.
- **O-019 clarified:** EDIT is an optional user application, not the boot screen; UX details remain deferred.
- **O-015 conflict to resolve:** older foreground-only audible audio contradicts later approved background audio/mixer concept. Review ADR-010 before implementation.
- **Storage policy conflict to resolve:** older deletion retention and file versioning drafts vs later manual-trash preference. Reconcile before SpiffFS implementation.
- **New P0:** exact syscall ABI, kernel/userspace boundary validation, reserve sizing and error contracts.
- **New governance requirement:** AI tasks must read root AGENTS.md and maintain the Developer's Guide and ADRs (ADR-011).

Keep original open items above as historical inventory; these status notes take precedence where explicitly approved.

## Conflict triage — 2026-10-26 review

- **Audio policy resolved:** use ADR-010's continued background playback and shared Audio Manager, consistent with the latest approval. Older foreground-only mute language is superseded. Exact mixer limits remain open.
- **Filesystem recovery policy remains a major subsystem decision:** older retention/versioning specifications conflict with later preference for manually managed trash. Obtain an explicit official choice before changing SpiffFS contracts.
- **Graphics implementation detail:** later approved default logical modes and opt-in collision behavior supersede older proposals where approval is clear; exact APIs/driver implementation still open. Avoid spending initial boot milestones on these details.

**Filesystem policy resolved 2026-10-26:** manual Trash, one current saved version, no automatic retention/purge; see ADR-012. O-008 remains OPEN only for implementation details (on-disk format, crash consistency, recovery, transaction semantics, compatibility and A/B safeguards). Do not treat older automatic versioning/retention policies as current.

## First-boot milestone — 2026-10-28

For the QEMU-only smoke test, the pinned Limine protocol and binary pair,
base revision 5, C entry and template-derived ELF layout are recorded in
[ADR-011](ADR-011-boot-kernel-memory-governance.md) and the
[boot record](../operations/first-boot-2026-10-28.md). The exact serial marker
was captured in two passing QEMU tests, including a fresh build. Future process
VM layout, runtime ABI, allocators, recovery/A-B and BASIC remain open.

## Scoped framebuffer diagnostic — 2026-10-28

The owner authorized a QEMU boot pattern as the next verified step after
`be74fe2`; see ADR-011. O-014 logical resolution, composition, sprites and
BASIC graphics interfaces remain open. This diagnostic settles none of them.

## Fixed boot label scope — 2026-10-28

The owner authorized a fixed bitmap SPIFFOS label after `8f48fa7`; see ADR-011.
General text encoding, console/cursor/scrolling behavior, font selection, BASIC
interfaces and logical graphics policy remain open.

## Boot console scope — 2026-10-28

A bounded kernel diagnostic console is authorized after `25150f0`; see ADR-011.
Scrolling, input, full text encoding, visible cursor, user console interfaces
and BASIC remain open and outside this step.
