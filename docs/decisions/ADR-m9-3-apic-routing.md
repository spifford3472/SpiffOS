# ADR M9-3D — APIC-first interrupt discovery and controlled IRQ0 routing

Status: owner-selected architecture (2026-11-11), implementation not yet validated or merged.
Supersedes the QEMU legacy-PIC-only delivery experiment in ADR-m9-3-irq0-delivery.md; does not supersede the tested Phase B masked-PIC fallback.

## Problem and evidence
QEMU q35 delivers PIT0 edges to the legacy PIC but its enabled local APIC masks LINT0 ExtINT, and firmware masks all I/O APIC redirection entries. The Phase D legacy PIC test failed to deliver any timer tick; see operations/m9-3d-irq0-blocked-20261111.md (draft PR #40, issue #41). Altering PIC masks cannot establish CPU routing.

## Decision and scope
Use ACPI RSDP, XSDT (RSDT fallback only if independently validated), and MADT to discover boot CPU LAPIC ID/address, I/O APIC MMIO base and global interrupt range, and IRQ0 interrupt-source overrides. The kernel must validate signatures, revisions, lengths, checksums, address arithmetic, table bounds, multiple controllers and unsupported bus flags before using any addresses. A validated ACPI physical address is NOT itself a mapped kernel pointer. Memory mappings, UC cache policy, and APIC MMIO accessibility require separate preflight and tests; do not blindly add HHDM offset to arbitrary MMIO. Detect unsupported layouts and fail closed with IF=0.

Initialize LAPIC and I/O APIC using explicit safe masks and known QEMU-tested topology; do not assume every x86 laptop has the same LAPIC base, APIC ID, GSI routing, polarity or trigger. Reserve a dedicated IRQ0 vector 0x30 outside CPU exceptions (0-31) and the existing PIC range (32-47). Route only the verified PIT0 GSI to the boot CPU; leave all other I/O APIC pins masked. Preserve PIT channel 2 used by F12 and keep keyboard and UART polled. Send LAPIC EOI after bounded timer tick; no serial prints, heap, blocking work or scheduling inside the ISR. Keep legacy PIC fully masked in APIC mode. Enable CPU interrupts only after IDT/IST, timer service, ACPI topology, mappings, LAPIC, IOAPIC redirection and IRQ0 handler are validated.

## Alternatives and tradeoffs
1. PIC-to-LAPIC ExtINT virtual-wire bridge: smaller QEMU-specific patch, but poor long-term laptop architecture; rejected as primary route.
2. Fixed QEMU I/O APIC address 0xFEC00000 and fixed LAPIC address 0xFEE00000: rejected; these are typical values, not safe hardware discovery.
3. ACPI MADT-discovered APIC path: selected; more implementation/testing but more extensible and explicit failure behavior.
4. Full ACPI interpreter or OS-wide driver framework: deferred; keep a small read-only parser and single boot-CPU routing adapter.

## Checkpoints and gates
A. Documentation-first approval and unmerged source branch; preserve known-good Phase C ISO.
B. Pure hosted MADT parser with exhaustive malformed-table negative tests; RSDP/XSDT selection and checksum tests independently; no MMIO or PIC changes.
C. QEMU read-only ACPI topology and APIC mapping discovery; compare MADT with QEMU firmware diagnostics. Fail without STI if unsupported.
D. Host-controlled and QEMU-tested LAPIC/IOAPIC IO layer, masks and redirection. Only then enable PIT0 IRQ0 via dedicated vector and verify ~100 Hz.
E. Positive and negative real-guest delivery tests; IRQ storm/missing tick safeguards; full QEMU 128/256/512 MiB, F1-F12 (1-second fade), framebuffer, keyboard editing, #UD/#DF, memory, serial regression suite.
F. Review and merge each checkpoint after verified match between tested source and GitHub; stage ISO separately, verify hashes and rollback. Never install on physical laptop or change Forge OS.

## Open / deferred
Memory-map-backed APIC MMIO cache attributes, APIC MSR state and x2APIC, SMP/per-CPU TSS, NMI/APIC error handling, high GSIs and multiple I/O APICs, dynamic hotplug, APIC timer, HPET, Wi-Fi, USB, ring3 and scheduler. Unsupported cases must retain a safe non-interrupting kernel and diagnostic output. Exact driver ABI remains internal.

## Owner decision
2026-11-11: owner answered “yes please proceed” to the recommendation for ACPI/MADT-first local APIC + I/O APIC interrupt routing, with PIC fallback. This authorizes design, isolated implementation/testing and validation-gated merge/deploy, not untested hardware modification.
