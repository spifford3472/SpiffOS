# Architecture decision register — 2026-10-20

These ADRs consolidate approvals made in design discussions; they do not authorize implementation, merge, or deployment.

- ADR-001: modular monolithic kernel, Ring 0/Ring 3 separation, built-in v1 drivers, isolated BASIC processes — ACCEPTED.
- ADR-002: process isolation, logical PEEK/POKE, workspace media, future SYS machine-code execution — ACCEPTED direction. Exact APIs, sprite v1 scope, and memory limits remain OPEN.
- ADR-003: custom UEFI loader, A/B boot slots, F8 and supervisor recovery, automatic rollback of failed pending updates, no countdown — ACCEPTED.
- Documentation-only PR: no kernel code, bootloader code, or system deployment.

Next reviews: boot metadata state machine; filesystem power-loss consistency; BASIC command signatures; sprite/media ownership; hardware support and driver ABI.
