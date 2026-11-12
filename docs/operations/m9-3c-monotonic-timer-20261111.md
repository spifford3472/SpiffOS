# M9-3C monotonic timer core — 2026-11-11

## Scope and status
An internal monotonic tick service has been implemented in an isolated Forge source snapshot based on M9-3B merge commit `19e2cac89363f9d14ff1eb1b96239767a5b5accf`. **It has no active hardware IRQ source until Phase D**; it reports unavailable rather than inventing elapsed time.

## Decisions and model
Read AGENTS.md, accepted/open decisions, architecture, developers guide, M9, timer/F12, exception and PIC ADRs, and memory references. Local QWEN 30B reviewed state transitions, saturating count, atomics, and PIT0 choice. Coordinator selected a single-CPU 64-bit saturating tick count, quotient/remainder milliseconds conversion, explicit first-tick availability and no channel-2 changes. See ADR-m9-3-monotonic-clock.md.

## Interfaces and initialization
`src/kernel/kernel-timer.[ch]` exposes caller-owned initialization, bounded tick note, availability-checked atomic read, integer millisecond conversion, elapsed-tick validation, and a static boot-CPU instance. Unit tests inject ticks without privileged instructions. `main.c` establishes 100 Hz intended frequency with IRQs still disabled, confirms no readable time before hardware ticks, and emits `SPIFFOS_TIMER_WAITING`. No wall-clock, heap, sleep, scheduling, or device IO in timer core.

## Test results
* `make test-timer-memory`: PASS, nulls, invalid frequency, uninitialized/unavailable state, first-tick transition, atomic read, saturation at UINT64_MAX, ms conversion bounds, and reversed elapsed samples.
* Strict Clang freestanding build: PASS.
* `make test-memory test-keyboard test-idt-fault test-double-fault`: PASS. Real-QEMU 128/256/512 MiB, memory/framebuffer and exact screenshots, keyboard/editor, F1–F9, F10, F12 lifetime 1.008 seconds, #UD and poisoned-stack #DF. Actual log: `/tmp/spiffos-m9-3c-regression.log`.

## Rollback and unfinished work
Candidate ISO is isolated under `/tmp/spiffos-m9-3c-rtremote/build/spiffos.iso`, not installed or activated in the owner checkout. Roll back to verified M9-3B merged source `19e2cac8` or preserved known-good owner ISO as appropriate. Phase D must configure PIT0 and validate actual IRQ0 delivery, masking, acknowledgments, tick rate and interrupt storms. PIT2 remains F12-private. APIC/HPET, wall clock, process scheduler and physical hardware are deferred. No QTH network or OPNsense changes were made.
