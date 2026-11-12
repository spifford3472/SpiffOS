# M9-3D blocked IRQ0 experiment — 2026-11-11

## Status
**STOP / NO MERGE / NO DEPLOY.** Owner-authorized QEMU-only experiment based on merged M9-3C `d3478fbfc9c4cd6ccaf118e5745eb2ccb78cf77a`. Source remains isolated at `/tmp/spiffos-m9-3d-rtremote`. Owner's checkout, ISO, Forge Linux, physical disks, network and OPNsense were not modified.

## Work attempted
Read AGENTS.md, decisions, architecture, M9 ADRs, M8 timer behavior, and prior validation reports. Local QWEN 30B reviewed the controlled IRQ0 design and verification sequence. Coordinator introduced 16 assembly PIC IRQ stubs with all general registers preserved, IDT gates 32–47, a bounded IRQ dispatch (timer tick and PIC EOI), PIT channel-0 mode-2 100-Hz setup, IRQ0-only unmask, and STI after keyboard readiness. PIT channel 2/F12 and PS/2/COM1 polling were unchanged. A new QEMU IRQ0 cadence test checks two 100-tick milestones; an isolated masked-IRQ0 test variant must fail the same gate.

## Actual tests
* `make test-idt-memory build/spiffos-kernel.elf`: PASS with strict freestanding Clang and hosted IDT tests.
* `make test-irq0`: **FAIL**. The positive real-QEMU 128-MiB guest completed boot, memory, framebuffer and keyboard initialization and printed `SPIFFOS_IRQ0_ENABLED` (subsequently renamed in draft source to `SPIFFOS_IRQ0_UNMASKED` for accuracy). It produced neither 100-tick nor 200-tick milestone within the bounded 12-second QEMU test. The negative masked-source test did not execute because the positive test failed first.
* Independent diagnostic QEMU run (45-second bounded lifetime) confirmed no tick milestones and no CPU exception panic. QMP/HMP in that live guest returned:
  * `info irq`: 1683 PIT IRQ0 signals observed by the virtual IOAPIC/8259 hardware at inspection.
  * `info pic`: `pic0: irr=01 imr=fe isr=00 irq_base=20`; `pic1: imr=ff`.
  * `info lapic`: local APIC `SPIV ... APIC enabled`, `LVT0 ... masked ExtINT`.
  * `info pic`: all IOAPIC redirection pins masked.
* No full memory/keyboard/fault regression suite was run on the failing Phase D candidate. Do not imply any test success beyond the scoped build/hosted IDT tests.

## Diagnosis and risk
QEMU PIT0 is generating IRQs and the legacy PIC recognizes IRQ0 as pending/unmasked, but the CPU's local APIC ExtINT connection is masked by the boot environment; the CPU never enters the IRQ0 IDT handler. Changing the PIC mask again cannot solve this. Do not bypass by changing QEMU machine defaults, unmasking arbitrary APIC MMIO, or claiming the timer is running because source code compiled. The last validated mainline release remains M9-3C, where all hardware interrupts stay disabled.

## Decision required before Phase D or E
1. **Virtual-wire bridge:** verify CPUID/MSR and mapped LAPIC state, then safely unmask LVT0 ExtINT to permit PIC delivery in QEMU; document limits and prohibit physical-hardware assumptions.
2. **APIC-first routing:** implement verified ACPI MADT hardware topology, local APIC and IOAPIC routing for PIT0, with explicit masks and proper APIC EOIs. Keep the PIC fallback but no longer rely on its path for primary IRQ delivery.

Recommend APIC-first for eventual real modern laptops if owner accepts the larger prerequisite scope. If only an isolated QEMU proof is needed, a verified virtual-wire bridge is smaller but becomes technical debt. No further D code should be merged until the owner selects the direction and the real-QEMU cadence/negative/regression gates pass.

## Rollback
No active installation to rollback. Resume from merged M9-3C commit `d3478fbfc9c4cd6ccaf118e5745eb2ccb78cf77a` and the preserved known-good owner ISO. The failed experiment should be retained only on a draft branch for review. No QTH infrastructure documentation is required because no network or external service configuration changed.
