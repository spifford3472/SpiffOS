# M9-2 CPU exception IDT validation — 2026-11-11

## Design and scope
See ADR-m9-exception-idt.md, ADR-m9-kernel-foundation.md, ADR-011,
ADR-m7-text-workspace-switching.md, ADR-m8-function-keys-and-workspace-indicator.md,
ADR-memory-services.md and AGENTS.md. PR #35 is the verified main baseline.

A static packed x86-64 256-entry IDT is loaded once using the current ring-0
CS and LIDT, after COM1 initialization. Vectors 0..31 contain dedicated
assembly stubs; vectors 32..255 remain absent and interrupts stay disabled.
Stubs normalize hardware vs synthetic error-code stack shapes before calling
the bounded, non-allocating serial fatal reporter. This checkpoint does not
provide IRQ delivery, PIC/APIC, EOI, timer IRQs, preemption, ring-3 signals,
double-fault IST/TSS or recovery from unusable stacks.

## Tested source and environment
Development was performed only in an isolated Forge checkout; the owner's
installed build/spiffos.iso and physical disks were not modified.
The pinned Limine binary and original 1280x800 framebuffer target remain.

QWEN: local qwen3-coder:30b generated a narrowly scoped x86-idt.h descriptor
interface. The coordinator reviewed/corrected its C11 static assertions,
contract comments and signature, and implemented the pure gate builder,
exception entry assembly, serial panic, tests, Makefile and documentation.
Codex not used.

## Actual test results
- make test-idt-memory: PASS, x86-64 IDT gate/IDTR exact sizes, all 32 gate
  encodings, 224 absent IRQ entries and unchanged outputs on invalid inputs
  under ASan/UBSan.
- QEMU test with keyboard readiness, framebuffer capture, 128 MiB: PASS,
  IDT_READY before BOOT_OK and KEYBOARD_READY.
- make test-memory: PASS, real OVMF QEMU 128/256/512 MiB with independent
  frame counters and all exact framebuffer pixels.
- make test-keyboard: PASS, real QMP keyboard/editing and workspace
  screenshot checks, including M8 measured F12 lifetime 1.009 seconds.
- QEMU keyboard test at 128 MiB: PASS; F1–F9, F10, F12 overlay fade/restore,
  editing and terminal readiness.
- scripts/test-exception-fault.py: PASS; builds test-only ISO in temporary
  copied checkout, executes UD2 at startup, verifies exactly one
  SPIFFOS_EXCEPTION:06,0000000000000000 and EXCEPTION_HALT, and verifies
  no BOOT_OK or KEYBOARD_READY. Runner nonzero status is expected
  for the intentionally halted guest.
- git diff --check: PASS.

## Recovery and limitations
Exception handling intentionally halts the guest. It has no dedicated fault
stack or double-fault recovery guarantee; fatal stack corruption may still
triple fault. The serial reporter uses a bounded polling loop per byte, not
a real-time deadline. NMI delivery is not masked by CLI and remains fatal.
Hardware interrupt vectors are deliberately not installed and STI must not be
executed. Physical-machine testing is deferred.

## Staging, deployment and rollback
A safe stage must use a separate ISO file and SHA256 checksum. Installation
is owner-only after backing up the PR #35 ISO; no physical partitions,
bootloaders, networking or OPNsense changes are permitted. Restore the prior
ISO and PR #35 commit to roll back; no persistent disk or network migration.
