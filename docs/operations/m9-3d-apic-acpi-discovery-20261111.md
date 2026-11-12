# M9-3D APIC-first read-only ACPI discovery — 2026-11-11

## Scope
First checkpoint after owner approved APIC-first routing. Isolated Forge source snapshot based on merged Phase C commit `d3478fbfc9c4cd6ccaf118e5745eb2ccb78cf77a`. Draft legacy PIC IRQ0 failure remains in PR #40 and Issue #41 for historical context; it has **not** been merged.

## Governance and design
Read root AGENTS.md, accepted/open registers, system overview, developers guide, M9 exception-stack/PIC/timer ADRs and blocked IRQ0 diagnostics. Local QWEN 30B was given a narrow ACPI discovery design review with explicit references; it recommended checksum/length validation and read-only ACPI mapping, but some example structures were not specification-correct. Coordinator implemented reviewed byte-wise layouts instead. No Codex used.

Owner's decision: use ACPI/MADT-discovered local APIC and I/O APIC routing as the primary architecture, retaining the PIC fallback. The choice and validation order are in `docs/decisions/ADR-m9-3-apic-routing.md`. This checkpoint does not enable external interrupts, configure LAPIC/IOAPIC registers, or replace any legacy PIC behavior.

## Implemented source
* `src/kernel/acpi-madt.[ch]`: pure, allocation-free MADT parser with SDT checksum, complete record bounds, enabled LAPIC count, IOAPIC address/ID/GSI base, optional IRQ0 source override, flags validation, and LAPIC base override. It rejects duplicate IOAPICs, multiple timer overrides, missing APIC addresses and malformed lengths.
* `src/kernel/acpi-discovery.[ch]`: validates RSDP 2+ classic/extended checksums and discovers a single MADT through validated XSDT table mappings, without dereferencing raw physical addresses. Caller-provided mapper enforces a trusted physical-address range.
* `src/kernel/main.c`: requests the bootloader's mapped RSDP, validates XSDT physical pointers with read-only Limine memory-map ACPI regions and HHDM, then emits `SPIFFOS_ACPI_MADT_OK` or `SPIFFOS_ACPI_MADT_UNAVAILABLE`. Neither outcome changes IRQ masks, MMIO registers, or F12.
* `tests/acpi-madt-test.c` and `tests/acpi-discovery-test.c`: hosted positive, missing, corrupted-checksum, truncated, duplicate, reserved-flags and out-of-range rejection tests.
* `Makefile`: separate hosted tests and kernel source objects.

## Actual test evidence
* `make test-acpi-madt test-acpi-discovery build/spiffos-kernel.elf`: PASS (ASan/UBSan hosted and strict freestanding Clang).
* `scripts/qemu.py test --memory-mib 128`: PASS with `SPIFFOS_ACPI_MADT_OK` and `SPIFFOS_BOOT_OK`.
* `make test-memory test-keyboard test-idt-fault test-double-fault`: PASS. Real QEMU 128, 256 and 512 MiB memory, exact framebuffer and snapshot comparisons, F1-F9/F10, F12 1.008 seconds, keyboard/editing, #UD and poisoned-stack #DF. Log `/tmp/spiffos-apic-full-regression.log`.

## Limitations and next checkpoint
ACPI read-only discovery does **not** prove that LAPIC or IOAPIC MMIO physical pages are safely mapped or that cache attributes are correct. Page-table mapping, LAPIC MSR and interrupt routing remain separate guarded tasks. Before any STI, add an explicit MMIO mapping/validation abstraction, reserve interrupt vector ownership, mask all IOAPIC pins, route exactly PIT0 IRQ0, validate APIC EOI with actual QEMU tick delivery and negative tests. No physical laptop, LAN, OPNsense, Forge Linux, or owner ISO modifications.

## Rollback
The owner working ISO and earlier backup are untouched. This checkpoint is a source-only additive discovery path; boot remains on previous PIC-masked, interrupt-disabled M9-3C behavior. To rollback source, restore merged Phase C commit `d3478fbfc9c4cd6ccaf118e5745eb2ccb78cf77a`. Do not install a new ISO until checksum and source match are verified.
