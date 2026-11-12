# ADR M9-3D5 — Read-only APIC hardware validation before IRQ0

Status: owner authorized implementation and tested merge/local Forge QEMU deployment, 2026-11-12. The prior M9-3D4 source was merged via PR #47. This ADR covers **read-only inspection**, not controller interrupt activation.

## Context
M9-3D4 maps ACPI-discovered LAPIC/IOAPIC physical register windows into two private 4KiB, supervisor-only, RW/NX, PAT-index-3-UC virtual pages, staged through M2/M3 kernel-owned page tables. The physical translation is independently checked in real QEMU at 128/256/512 MiB; the M9-3D3 HHDM probe remains unverified. A valid virtual PTE alone does not prove the device is safe to touch: CPU effective memory type, possible aliases, APIC identity, and I/O APIC GSI capacity are unvalidated.

## Decision
Keep IF=0, legacy PIC all masked, no PIT0, no IRQ gates, no LAPIC EOI, no redirection writes, no APIC interrupt configuration. Validate current CPU xAPIC capability/APIC-base MSR and firmware MADT before interpreting any APIC registers. Validate PAT index 3 UC, NX, page ownership and complete live private translation; inspect MTRR state conservatively and reject unknown effective memory types. Reject or explicitly mark unverified existing physical aliases whose effective memory types conflict; the private UC mapping is not a license to access arbitrary HHDM pointers.

Read the LAPIC local ID register at offset 0x20 through the validated private MMIO address only after the cache/mapping safety gate. Verify the LAPIC ID equals CPUID leaf1 EBX[31:24] and appears as a valid enabled local APIC in the MADT; unsupported/ambiguous ACPI entries fail closed. For the I/O APIC the architecture requires writing **IOREGSEL** to choose a register before reading IOWIN. This selector write does not modify an IRQ mask/redirection record, but it is still a hardware MMIO write. Treat it as a narrowly scoped selector operation; do not describe this as *zero hardware writes*. Only select ID register 0x00 and version register 0x01, then read bounded IOWIN; no changes to IRQ redirection, LAPIC state, interrupts or any other controller register. Validate IOAPIC ID, nonzero supported entry count and MADT IRQ0 GSI in its actual range.

In case IOREGSEL selection is unsafe or conflicts with a strict policy for all hardware writes, stop after LAPIC-read validation and leave the IOAPIC inspection open rather than pretending its version was verified. Read-only means no **configuration** changes, except the unavoidable selector latch.

## Tests and acceptance
Hosted, sanitizer-enabled tests inject bounded fake MMIO registers and synthetic ACPI/CPUID/MTRR/alias state; verify rejecting absent/bogus/unmapped devices, invalid/cache/mismatched ID, malformed version, out-of-range GSI, and no redirection side effects. Real QEMU q35 must show positive validated LAPIC ID and IOAPIC ID/version when safe; a negative guest path must skip MMIO and remain normally bootable with IF=0. Run all 128/256/512 MiB memory, exact framebuffer, F1-F9/F10/F12, editor, #UD/#DF tests. Validate tested-source hashes; only then merge and stage a backup-protected owner ISO. Never run on real Anvilone/physical media.

## Alternatives and deferrals
Blind HHDM MMIO dereferencing and fixed QEMU APIC constants are rejected. Firmware non-QEMU/multiple APIC/mixed caching layouts may be marked unsupported, but must never silently enable interrupts. APIC routing, masking, EOI, PIT IRQ0 and STI are a separate M9-3D6 work item and remain disabled even after successful read-only inspection.

## Approval and history
2026-11-12 owner said "lets do your recommended next step, you can merge and deploy when you complete the step" after recommendation to do M9-3D5 read-only APIC hardware validation. This authorizes safe code and QEMU-only staging subject to all regression/negative gates, not uncontrolled hardware modification.
