# ADR M9-3D4 — Kernel-owned, uncached APIC MMIO aperture

Status: owner approved design/implementation on 2026-11-12. No active MMIO or hardware interrupt delivery is accepted until independent runtime and regression gates pass.

## Context
Read-only APIC discovery (`#42`), CPU capability comparison (`#43`), and existing paging inspection (`#44`) are merged. The QEMU q35 guest discovers a valid MADT and matching IA32_APIC_BASE MSR but cannot prove a safe current mapping for *both* LAPIC and IOAPIC pages: `SPIFFOS_APIC_MAPPING_UNVERIFIED`. The unsuccessful PIC-only IRQ0 experiment is in draft PR #40; Issue #41 preserves the negative evidence. The current M2/M3 allocator can allocate and own RAM pages, but has no general page-table insertion API. A physical firmware controller address is not a virtual pointer, and an arbitrary `HHDM+physical` access is not permitted.

## Decision (owner approved 2026-11-12, Issue #45)
Develop a minimal **private, fixed, supervisor-only two-page virtual MMIO aperture** for the boot CPU: one 4-KiB page each for the ACPI-discovered LAPIC and IOAPIC physical register windows. Every address must come from validated MADT/CPU checks and be page-aligned. The fixed virtual addresses are internal kernel implementation details, not ABI. Prove they are canonical, unused, and absent from existing kernel mappings before installing page entries. Do not alter the current direct map or any preexisting mapping. Fail closed if the virtual reservation conflicts.

Use **owned, zeroed RAM page-table frames** allocated through the existing M2/M3 ownership accounting as needed. Only supervisor/kernel ordinary allocation; no supervisor reserve depletion; 4- and 5-level x86 paging handled explicitly. Keep root and all borrowed tables owner-validated, preserve existing entries, and commit leaf entries last. A partially installed path must be removed and every newly allocated page returned to the same owner on failure; avoid releasing still-linked paging structures. Any recovery failure is fatal and must keep IF=0. Invalidate touched TLB translations when publishing/unmapping; no CR3 replacement or firmware table reclamation. All mapping operations run on the boot CPU with interrupts disabled and no SMP support. A bounded, pure map-plan/leaf-policy validation layer must be hosted-tested before any live PTE writes.

Use a **verified UC effective memory type**. Use PTE PAT=0, PCD=1, PWT=1 (PAT index 3) **only after verifying IA32_PAT index 3 equals UC**; where required, validate MTRR interaction, otherwise fail closed. Do not assume ACPI addresses are RAM or that any existing HHDM alias is appropriately cached. The MMIO aperture must be writable, supervisor-only, non-executable (NX) with full PTE-ancestor checks. No speculative direct MMIO until mapping and cache verification succeed. If the CPU does not support NX or the mapping/cache contract cannot be met, do not map/register-access. Separate IO backend should accept mapped virtual addresses only after a successful mapping commit.

## Alternatives
1. **Direct HHDM+physical:** rejected; mapper could not verify it, and cache attributes unknown.
2. **Hard-coded QEMU APIC addresses:** rejected as unsafe for portable x86 laptop support.
3. **Replace Limine CR3 or remap all HHDM:** rejected; large risk and unnecessary scope.
4. **Kernel-owned narrow page-table additions:** selected; understandable, testable, bounded and rollback-capable, with careful page-table ownership requirements.

## Validation, stop conditions and rollback
1. Documentation/checkpoint baseline committed before kernel page mutation.
2. Hosted ASan/UBSan tests for cache/PAT policy, leaf PTE construction, reserved virtual-address collisions, 4/5-level paths, missing/invalid parent, OOM, wrong owner, fault injection at every allocation/link, rollback/retry, and no partial publication.
3. Read-only QEMU preflight: validate CR3 page-table residency, map ownership, unoccupied virtual slots, CPUID NX and IA32_PAT UC policy. Do not attempt MMIO on unsupported topology.
4. Only if preflight passes, create exactly two kernel-private mappings. Re-walk page tables to confirm physical translation, supervisor-only, read/write, NX and uncached policy. Check LAPIC ID and IOAPIC version in a separate, single-register read-only checkpoint before any controller programming.
5. Run full memory 128/256/512 MiB, exact framebuffer, F1–F9/F10/F12 (one-second overlay), keyboard/editor, serial, `#UD` and IST1 `#DF`. Compare tested Git SHA/blob digests against PR; never overwrite owner ISO without a verified backup/matching source.
6. Failure => no IRQ unmask, no `STI`, no controller writes and no physical laptop deployment. Roll back to M9-3D3 merge `9892a1518845a2020840aebfdc6f7dc9d90a6368`. No changes to Forge Linux, OPNsense, 192.168.9.0/24 or 192.168.10.0/24.

## Out of scope
IRQ0 routing, LAPIC EOI, multiple IOAPICs, ACPI shutdown, scheduler, ring3, general public DMA/MMIO ABI, USB boot or disk changes, online/AP cores, IOMMU. Actual hardware IRQ0 remains a **separate validated checkpoint** after mapping completion.

## Approval history
2026-11-12: owner responded "i approve" to the recommendation for the dedicated MMIO mapping architecture in Issue #45. This explicitly authorizes designing and implementing the safe mapping foundation subject to above gates; it does not authorize bypassing a failed gate.

## Implementation evidence — 2026-11-12
The owner-approved mapping foundation is implemented in a guarded, bounded boot-CPU path, using M2/M3-allocated ordinary kernel page-table frames and one root-entry commit. Hosted tests and real QEMU 128/256/512 MiB positive/negative tests pass; the exact private LAPIC/IOAPIC virtual mappings are re-walked and checked for RW, NX and UC-index-3 flags before `SPIFFOS_MMIO_MAPPED_OK` is emitted. No MMIO register was read and IF remains zero. See `docs/operations/m9-3d4-mmio-mapping-20261112.md` for test commands and rollback. The remaining independent acceptance gate is *actual* APIC MMIO register accessibility and memory-type alias/MTRR safety; do not confuse a successful page-table mapping with validated hardware interrupts.
