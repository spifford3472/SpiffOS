# ADR M9-3D — Controlled PIT0 IRQ delivery and timer activation

Status: **BLOCKED / UNMERGED EXPERIMENT**, owner-authorized M9-3 Phase D, 2026-11-11. Do not treat PIC IRQ0 as functional on q35. No physical hardware or broad interrupt enablement authorized.

## Preconditions
Phase A implements an IST1 double-fault stack, Phase B remaps a masked dual PIC to vectors 32–47, and Phase C provides a saturating 64-bit monotonic tick core. Existing PS/2 keyboard and COM1 drivers poll, and the F12 overlay uses PIT channel 2. All baseline QEMU memory, keyboard and fault tests passed.

## Choice and alternatives
Install ring-0 0x20–0x2f gates with audited assembly stubs, save **all** general-purpose registers and align the stack for the SysV C ABI. No heap, logging or unbounded loops in IRQ context. Use a single IRQ0 timer source by programming PIT channel 0 for approximately 100 Hz (1,193,182-Hz oscillator divided by 11,932), unmask **only** master IRQ0, then execute STI after all handlers and services are ready. IRQ1 keyboard remains masked and polling. Channel 2 must not be touched by the new timer driver. PIC paths on QEMU are an interim platform choice, not assumed to work on x86 laptops without verified discovery.

APIC/HPET setup now would require ACPI MADT routing, MMIO and independent tests; TSC would need calibration and invariance validation. Both are deferred. No preemption or process scheduler in this milestone.

## Ordering and ownership
1. CLI, serial and TSS/IDT setup; IDT includes exceptions 0–31 and all PIC vectors 32–47.
2. PIC remap, with all IRQs masked and IF=0. Initialize clock core and memory/editor services normally.
3. After keyboard/workspace readiness, configure only PIT channel 0. Verify boot PIC and timer-ready state.
4. Unmask PIC IRQ0 while IF remains 0.
5. Execute STI once. First real IRQ0 calls bounded tick increment, then acknowledges master PIC.
6. The normal diagnostic input loop polls PS/2, serializes a one-time tick progress marker at 100 and 200 ticks from non-IRQ context, and keeps F12 channel-2 logic unchanged.

Unexpected IRQ1–15 handlers must acknowledge after masking the source where feasible, and never corrupt the keyboard or serial. IRQ7/15 spurious logic follows the ISR check from Phase B. IRQ2 cascade is reserved, with no slave unmask. An absent PIC/PIT may fail to deliver IRQ0; the user-visible timer remains unavailable and the actual QEMU test fails rather than pretending success.

## ABI, guards and future hardening
The common entry saves RAX/RBX/RCX/RDX/RSI/RDI/RBP/R8–R15, aligns RSP before calling C, and restores the original stack/registers before IRETQ. This kernel is built with no red zone and general registers only. CPU IF is masked automatically by an interrupt gate; timer handler must not reenable interrupts. No stack guards for ordinary IRQs, no SMP ownership, no ring-3 ABI, and no scheduling.

## Mandatory tests
Hosted IDT gate bounds and IRQ-vector ownership; PIC invalid input/masking/spurious EOI; timer arithmetic. Real QEMU 128/256/512 MiB boots, monitored PIT0 tick markers with realistic elapsed cadence and no premature timer availability, F12 measured ~1 sec, keyboard/editor screenshots, #UD/#DF fatal behavior, serial output and all allocator regressions. Test IRQ0 missing/delivery failure by a test-only build with IRQ0 intentionally disabled, and confirm a bounded timeout rather than false success. Refuse merge if actual guest PIT delivery or responsiveness fails.

## Rollback
Never overwrite owner ISO without verified backup and matching source/image SHA. Phase C merged commit is the rollback source for IRQ delivery. If IRQ0 faults, stop before merging; preserve previous operating configuration and document the failure.

## Real-QEMU failure and architecture decision required (2026-11-11)

The initial Phase D candidate compiles and installs IRQ0–15 IDT gates. It programs PIT0 mode 2 at approximately 100 Hz, unmasks only PIC IRQ0, and executes STI. However, the QEMU test cannot observe even 100 elapsed ticks within the bounded 12-second deadline. Serial reached `SPIFFOS_BOOT_OK`, `SPIFFOS_KEYBOARD_READY` and the PIC-unmask marker with no panic. QMP/HMP inspection of the running q35 VM showed:

* `info irq`: PIT IRQ0 was generated repeatedly (1683 observed at inspection time).
* `info pic`: master `irr=01 imr=fe isr=00` — IRQ0 pending and unmasked; slave `imr=ff`.
* `info lapic`: local APIC enabled, `LVT0 ... masked ExtINT`, and IOAPIC redirection pins masked.

Therefore the legacy PIC source has no CPU delivery path under current firmware/APIC routing. The build is **not working**, no positive IRQ0 cadence proof exists, and no regression-suite completion claim is valid for D. Stop before merge or installation. Candidate options for owner architecture decision: (1) implement a verified LAPIC LINT0 ExtINT virtual-wire bridge, keeping the PIC temporarily, or (2) establish ACPI/MADT-discovered local + I/O APIC routing and make that the primary architecture. Do not silently change the QEMU machine to hide the problem or unmask LAPIC registers without separate design and validation.
