# M9-2 — x86-64 exception-only IDT and fail-stop diagnostics

Status: owner-authorized incremental implementation, 2026-11-11; test gate required before merge.

## Context

Current kernel executes with IRQs disabled and polls PS/2. There is no installed
Interrupt Descriptor Table, so CPU faults risk a triple fault, hiding error
evidence. M9 asks for safe CPU exception handling before enabling IRQs.
Limine supplies the boot CPU stack. COM1 uses bounded polling, and F12 uses PIT
channel 2 without interrupt delivery. Preserve both.

## Decision

Construct an internal static, 16-byte-entry x86-64 IDT with vectors 0..31
populated, and an IDTR pointing at it. Vector 0..31 stubs normalize the stack
to an exception number and a machine error code (synthetic zero where the CPU
did not push one), save volatile general-purpose registers, and invoke a
noreturn C serial panic reporter. The panic path writes a bounded fixed-format
exception vector and numeric error code and executes CLI + HLT forever.
Do not call any allocator, change paging, reset devices, or attempt to return
from an exception. Exception recursion or an unavailable serial port may halt
without complete evidence; bounded UART writes must not hang forever.
Reserve an IST/TSS/guarded exception stack for later design; M9-2 does not
pretend to survive a destroyed current stack or double faults.

The IDT is loaded only after the existing UART is initialized. No IRQ entries
are installed; **STI must not execute**. No PIC/APIC remapping, EOI, scheduler,
syscall ABI, or ring 3 is added. The installed IDT uses current CS from a
documented ring-0 read and interrupt gates (IF is masked on entry).

## Alternatives and trade-offs

- Leaving faults unhandled loses diagnostics; rejected.
- Enabling PIC timer interrupts immediately risks latent keyboard and F12
  regressions; deferred.
- Compiler interrupt attributes create different frame/error-code signatures,
  make preserving registers compiler-dependent, and require precise stack
  configuration; choose small audited assembly stubs.
- Dedicated IST fault stacks add TSS/GDT work beyond this first checkpoint;
  document limitations and handle them in later milestones.

## Validation and safety gates

1. Hosted deterministic/sanitized tests verify IDT entry encoding, vector
   bounds, absent IRQ gates, selector/type/present fields, and immutable failure.
2. Freestanding Clang + assembler + linker build and real QEMU normal boot.
3. Controlled QEMU fault injection in *test-only compile-time variant* validates
   exactly one exception diagnostic and non-return to the editor; no persistent
   run-time fault trigger or unbounded firmware callback.
4. Existing 128/256/512 MiB memory, keyboard, F1–F9/F10/F12 screenshot and
   timing tests must continue to pass.
5. Stage ISO separately and preserve previous owner-owned ISO and rollback.
   Never touch physical laptop, partitions, OPNsense or LAN services.

## Open

Error-code list compatibility across x86 extensions, IST/TSS protected stacks,
stack alignment after all CPU traps, nested faults, APIC/PIC/IRQ controller, and
eventually ring-3 fault recovery require independent tests and decisions.
