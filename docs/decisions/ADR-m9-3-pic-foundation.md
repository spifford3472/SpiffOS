# ADR M9-3B — Masked legacy PIC interrupt-controller foundation

Status: owner-authorized M9-3 Phase B, implementation candidate, 2026-11-11. No CPU interrupt enable until Phase D validation.

## Context
SpiffOS currently uses the boot CPU with IF=0, static IDT exception gates 0–31 and IST1 for double faults. PS/2 input is polled; F12 clocks elapsed time using PIT channel 2 without an IRQ. QEMU q35 exposes an 8259-compatible interrupt path, while modern x86 laptops may require ACPI-described local/I/O APIC hardware.

## Alternatives
* **Dual legacy 8259 PIC for first QEMU IRQ:** fewer moving pieces, easy IO-port and acknowledgment tests, adequate for one IRQ0 source. Limited to 15 effective IRQ lines and not a final hardware-portability contract.
* **Local + I/O APIC now:** better eventual laptop portability, but requires verified ACPI/MADT interrupt topology, MMIO mapping, interrupt delivery mode and additional error paths. Defer until those prerequisites are owned and tested.
* **Enable IRQs with firmware mappings:** rejected because conflicts with CPU exception vectors and undocumented firmware masks.

## Phase B choice
Create a small boot-CPU-only dual 8259 driver with caller-injected IO callbacks for hosted tests and direct x86 IN/OUT for QEMU. Enter only while IF=0. Explicitly mask every source before ICW setup, remap master IRQ0–7 to IDT 0x20–0x27 and slave IRQ8–15 to 0x28–0x2f, set master-slave cascade IRQ2, then restore **all masked** (not firmware masks). No STI, PIT0 reprogramming or IRQ unmask in Phase B.

The driver's state tracks initialization, 16-bit mask shadow and IO transport. Mask/unmask must reject invalid IRQ numbers; unmasking a slave source must account for cascade IRQ2 and never be the first Phase B action. Unhandled sources must be masked and safely acknowledged in the eventual Phase D handler. Ack IRQ8–15 slave first, master cascade second; IRQ0–7 master only. For spurious IRQ7, ISR bit7 clear means no EOI. For spurious IRQ15, ISR bit7 clear means master-only EOI. Read ISR using OCW3 0x0B, restoring no borrowed state.

**Vector ownership:** 0–31 CPU exceptions; 32–39 master PIC IRQ0–7; 40–47 slave PIC IRQ8–15; 48–255 unassigned and not-present until separately designed. IF stays zero in B and C, and no hardware IRQ gates are present yet.

## Failure, resource and portability policy
No heap or long polling in the PIC driver. A rejected input makes no IO writes. Remapping itself cannot reliably prove PIC presence on arbitrary hardware; Phase D requires actual QEMU IRQ0 delivery before declaring this path working. Unsupported physical targets must not enable IF or treat PIC success as a portable platform check. All other IRQ lines remain masked until an owning driver, handler and negative tests are available.

## Validation
Hosted deterministic register-order/mask/ack/invalid-IRQ tests, strict freestanding build, memory/keyboard/workspace real-QEMU regressions at 128/256/512 MiB. Phase B may log PIC ready while interrupts remain disabled. Separate Phase D tests must verify IRQ0 delivery, mask/unmask, EOI, stress and no storms. No direct physical deployment or LAN changes.

## Approval history
2026-11-11: owner's M9-3 prompt authorizes comparing PIC/APIC and choosing the simplest safe QEMU foundation. Implementation is incremental, not an approved permanent interrupt-controller ABI.
