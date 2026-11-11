# ADR M9 — Kernel foundation and staged hardware services

Status: owner-authorized M9 scope, 2026-11-11. Implementation and validation are incremental; no interface is declared stable until tested.

## Context and observed baseline
M8.1 has nine retained framebuffer/editor workspaces, F1–F9 selection, F10 system view, and an F12 overlay timed through PIT channel 2. The current kernel runs on one boot CPU with interrupts disabled, polls PS/2, and writes serial output through a bounded COM1 loop. Existing owned physical frames, protected diagnostic reserve, checked page access, heap and retained roots must not be replaced needlessly. No ring-3 execution, syscalls, interrupt dispatcher, portable monotonic timer, or device registry is implemented.

Read AGENTS.md, accepted.md, open.md, ADR-011, ADR-memory-services, ADR-m7 and ADR-m8 before implementation.

## Decision and sequencing
1. Preserve M8.1 ISO and create an isolated development tree. Keep existing QEMU serial test markers.
2. First introduce a small kernel-local result/error vocabulary with no allocation or hardware dependencies, and hosted tests. Existing allocator-specific error enums remain authoritative; no forced migration.
3. Design an x86-64 exception-only IDT with dedicated early panic path and no heap allocation. Do not execute STI until all IRQ entries, controller masks and acknowledgments have verified tests.
4. Establish a monotonic source with documented wrap handling and bounded polling. Preserve PIT channel-2 F12 behavior until the replacement passes measured real-QEMU tests.
5. Use a versioned, static driver table with explicit initialization state and capability bits. Register the existing bounded serial driver first; no dynamic modules or plug-and-play at M9.
6. Define task identifiers, state transitions and workspace ownership in a testable, scheduler-independent component. No preemption or user-mode execution in M9.
7. Add diagnostics and fail-safe paths without persistent disk writes.

## Alternatives and trade-offs
Avoid wholesale allocator replacement (regression and ownership risk); avoid interrupt-driven scheduling now (cannot justify safety before IDT/controller validation); avoid full plug-and-play and dynamic driver allocation (unnecessary boot and API complexity). A polling-based monotonic service remains a temporary implementation until platform timer selection is validated.

## Acceptance gates
Every incremental PR includes hosted unit/sanitizer tests and existing make test gates; interrupt/serial/timer integration additionally requires QEMU serial and fault-injection proof. F1–F12, editor contents, workspace snapshots and F12 measured duration are hard regressions. Record physical-hardware testing separately. Never overwrite the owner ISO without a verified backup.

## Open decisions
Final syscall ABI, ring-3 page mapping, IRQ controller (PIC/APIC), source selection beyond PIT, SMP, driver ABI stabilization, task scheduling and preemption, hardware timer portability, and filesystem crash-consistency mechanisms remain open. Do not silently settle them through this milestone.

## Scope/safety
Forge-only isolated checkout and QEMU. No installation to physical storage, bootloader, firewall, service, or LAN configuration change. No infrastructure repo commit is required for read-only local development.
