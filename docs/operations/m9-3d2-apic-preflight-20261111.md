# M9-3D2 APIC CPU capability preflight — 2026-11-11

## Scope and status
Owner approved APIC-first direction. This is a read-only CPU capability and firmware topology comparison on a new isolated Forge source tree based on M9-3D1. Does **not** create MMIO mappings, touch LAPIC/IOAPIC registers, unmask interrupts or enable STI.

## Work and model review
After reading AGENTS.md, accepted/open registers, system overview, developers guide, APIC routing ADR and memory/IDT references, the local QWEN 30B reviewed the next mapping gate. Its proposal contained factual mistakes (CPUID APIC bit is leaf 1 **EDX bit 9**, not ECX, and IA32_APIC_BASE address is in physical bits 12–51). Coordinator corrected these before implementing code. No Codex needed.

## Implementation
`src/kernel/apic-preflight.[ch]`: boots with IF=0 and checks CPUID leaf 1 EDX APIC bit, IA32_APIC_BASE MSR 0x1B local APIC enabled, x2APIC off, bootstrap processor bit, ACPI MADT physical LAPIC base versus MSR, page alignment of LAPIC/IOAPIC bases and IRQ0 GSI range preconditions. No MMIO reads/writes. Failure produces a bounded serial unavailable marker; no fallback interrupts enabled.
`tests/apic-preflight-test.c`: hosted rejection and valid synthetic CPU/MADT snapshots. `Makefile`: freestanding source target and hosted ASan/UBSan tests.

## Verified tests
* `make test-apic-preflight build/spiffos-kernel.elf`: PASS (hosted and strict Clang).
* Real QEMU 128 MiB: PASS, serial `SPIFFOS_ACPI_MADT_OK`, `SPIFFOS_APIC_CPU_MATCH`, `SPIFFOS_BOOT_OK`.
* Full `make test-memory test-keyboard test-idt-fault test-double-fault`: PASS. Real QEMU 128/256/512 MiB boots, exact framebuffer/workspace/keyboard behavior, F1-F9, F10, F12 lifetime 1.007 seconds, #UD and poisoned-stack #DF. Evidence `/tmp/spiffos-apic-preflight-full-regression.log`.

## Limits, rollback, change history
A matching APIC MSR is not a validated memory mapping. LAPIC/IOAPIC MMIO mapping, PAT/UC cache policy, page table ownership, IOAPIC pin enumeration, redirection programming and EOI remain OPEN. A failed preflight cannot silently enable PIC IRQs. The owner working ISO, Linux installation, LAN, OPNsense and physical devices are unchanged. Revert source to M9-3D1 merge commit `8c58ba43bd142f7c4929cee624775169564eeada` if this checkpoint regresses; do not install a new ISO without matched-source build and checksum verification.
