# ADR M9-3A — Boot CPU TSS and dedicated double-fault stack

Status: implementation candidate, owner-authorized M9-3 scope, 2026-11-11. Requires real-QEMU validation before merge.

## Context
M9-2 installs fatal CPU exception handlers but relies on the interrupted stack. Double-fault delivery after stack exhaustion can triple fault. Interrupts remain disabled; the loader owns the initial GDT, and the kernel uses its existing CS selector.

## Choice and rationale
Retain all active GDT selector offsets and descriptor bytes in a static boot-CPU-owned GDT, append a 16-byte available 64-bit TSS descriptor, then load GDTR and TR with IRQs disabled. Preserve existing CS and data selectors. A static 104-byte TSS points IST1 at the top of a separate aligned 16 KiB kernel-owned emergency stack. Assign IST1 **only** to exception vector 8 after the task register is confirmed. No heap allocation and no general IRQ handling.

A self-owned GDT avoids writing the loader's memory. Copying the existing descriptor bytes avoids the hazard of assuming fixed Limine selectors or reloading CS to an unverified descriptor. Reject GDTs above the chosen 8 KiB cap or with malformed size before changing GDTR; do not silently substitute new segment layouts. Current TSS I/O bitmap offset disables the I/O bitmap.

## Alternatives and trade-offs
* Leave loader GDT alone and write a TSS descriptor there: rejected because loader memory ownership and available selector space are not guaranteed.
* Replace GDT with hand-built code and data descriptors: rejected for this checkpoint because it could alter bootloader-selected selectors and cached attributes.
* Attach IST to every exception: deferred, as it may hide stack exhaustion and unnecessarily consume the dedicated emergency stack.
* Guard pages around the emergency stack: not included yet; requires validated page-table editing and fault containment. The stack is dedicated but **not guarded**.
* PIC/APIC interrupts and preemption: explicitly excluded from Phase A.

## Boot order and ownership
With CLI already executed, initialize the static GDT/TSS before installing the IDT. Keep all prior loader descriptors at unchanged selector offsets. Test TR with STR, then use an IST1 gate only for vector 8. The stack and TSS live in static kernel data for the lifetime of the boot CPU. No user-facing or allocator resource is transferred. If initialization fails, emit bounded serial failure and halt; never enable interrupts.

## Validation and remaining limits
Hosted tests must check descriptor bytes, invalid arguments and unchanged failure outputs. Guest tests must verify TSS readiness, regular exception reporting and deliberate double-fault IST delivery (a controlled QEMU-only test image), plus ordinary 128/256/512 MiB boots, F1–F12 and framebuffer integrity. Failure to reproduce double-fault delivery means Phase A cannot be declared complete. No claim of recovery from arbitrary stack or page-table corruption, no SMP, ring 3, or portable GDT ABI.

## Decision history
2026-11-11: owner approved the M9-3 exception-stack phase in the attached milestone prompt. Coordinator reviewed baseline; local QWEN recommended a self-owned GDT/TSS and dedicated IST. Coordinator refined to preserve all active selector slots byte-for-byte rather than inventing descriptor values.
