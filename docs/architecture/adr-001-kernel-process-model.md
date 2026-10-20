# ADR-001 — Kernel and process architecture

Status: ACCEPTED (design direction; implementation pending)
Date: 2026-10-20

## Context
SpiffOS is a native x86-64, BASIC-first operating system, not a C64 emulator or Linux distribution. It should be small, understandable, reliable, and extensible.

## Decision
- Use a modular monolithic kernel for v1.
- Ring 0 owns kernel, scheduler, virtual memory, interrupts, essential hardware drivers, filesystem core, and IPv4/TCP/UDP/socket management.
- Ring 3 runs BASIC interpreters, editor, applications, and appropriate user services, including DNS and higher-level network programs.
- Rings 1 and 2 are unused.
- Compile v1 drivers into the kernel behind documented modular interfaces; dynamic kernel modules are deferred.
- Provide timer-driven preemptive scheduling and process isolation.
- Support nine workspaces, with multiple independent BASIC processes in each workspace and named message-channel IPC.
- Keep the emergency supervisor separate from BASIC.

## Consequences
Kernel defects remain high impact; clear internal interfaces and tests are essential. Ring 3 programs cannot access hardware or other processes' memory directly.

## Open implementation details
Scheduler quantum and fairness, IPC limits and delivery guarantees, exact driver ABI, and hardware support matrix.
