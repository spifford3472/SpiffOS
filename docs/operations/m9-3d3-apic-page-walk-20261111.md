# M9-3D3 exploratory read-only APIC page-table validation — 2026-11-11

## Purpose
Owner approved APIC-first. After M9-3D1 ACPI discovery (PR #42) and M9-3D2 CPU/MSR preflight (PR #43), inspect existing kernel page tables without changing them. This is NOT a complete APIC IRQ0 implementation.

## Scope and design
Implemented `src/kernel/x86-page-walk.[ch]`: a pure 4/5-level page-table walker using a caller-injected physical table-page mapper, present/supervisor checks and 4KiB/2MiB/1GiB leaf support; no allocation, page writes or device MMIO access. Implemented `tests/x86-page-walk-test.c` with present/missing, huge-page, user-bit, noncanonical, invalid CR3 and mapper rejection. Read-only boot adapter only borrows page tables in Limine-reported RAM regions through HHDM, never treats firmware MMIO as RAM, and verifies physical translation of MADT-discovered LAPIC/IOAPIC addresses. No device registers are read or written.

## QEMU finding
The 128 MiB guest booted and reported `SPIFFOS_ACPI_MADT_OK`, `SPIFFOS_APIC_CPU_MATCH`, and an initial `SPIFFOS_APIC_PAGES_UNMAPPED` marker. Because an unavailable intermediate page-table mapping also yields false, the message was corrected before final tests to `SPIFFOS_APIC_MAPPING_UNVERIFIED`. This finding is not a device-absence claim. Do not route IRQs based on this probe; no physical mapping or correct UC cache attributes have been proven.

## Tests
Hosted `make test-page-walk` and strict kernel build: PASS. Full `make test-memory test-keyboard test-idt-fault test-double-fault`: PASS, real QEMU 128/256/512 MiB boots, memory, exact framebuffer/workspaces, F1-F9, F10, F12 1.008 seconds, keyboard and #UD/#DF. Evidence `/tmp/spiffos-apic-pages-regression.log`.

## Safety and next gates
No LAPIC/IOAPIC MMIO reads/writes, page-table mutation, STI, PIT0 changes, IRQ0 routing, scheduling, network or physical hardware changes. Do not assume HHDM covers MMIO. The next distinct work package is a controlled MMIO virtual-address reservation and page-table mapping service (UC/PAT/MTRR, frame ownership, rollback, actual QEMU proof), followed by exactly one IOAPIC IRQ0 redirection and timer tests. User-owned Forge checkout and ISO files remain untouched. Last merged safe source is M9-3D2; use its merge SHA as rollback and keep candidate source staged separately.

## Model contributions
Local QWEN 30B reviewed the paging preflight design, while coordinator independently corrected its incorrect APIC CPUID bit and unsafe suggestions concerning automatic frame ownership and cache inference. No Codex used. Root AGENTS.md, all mandatory decisions and subsystem docs were consulted.
