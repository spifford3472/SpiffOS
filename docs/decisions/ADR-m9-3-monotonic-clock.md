# ADR M9-3C — Monotonic kernel tick service

Status: owner-authorized M9-3 Phase C, preliminary implementation choice 2026-11-11. No external IRQ enabled until Phase D.

## Context
The existing F12 workspace overlay measures one second by explicitly programming and polling PIT channel 2. This path must not regress. Phase B has a fully masked legacy PIC, but no IRQ handlers. The first useful bounded kernel monotonic service should have a small internal API independent of wall-clock time and handle unavailable hardware safely.

## Alternatives
* TSC calibrated during boot: submicrosecond precision but frequency invariance/CPU migration and calibration require broader hardware discovery; defer.
* PIT channel-2 polling shared with F12: conflicts with F12's private reset and might distort user timing; reject.
* HPET or local APIC timer: requires ACPI/MMIO/APIC support not approved at this checkpoint; defer.
* **Dedicated PIT channel-0 IRQ0 tick** on QEMU: simple and testable at 100 Hz after safe PIC/IDT setup in Phase D. Do not reprogram channel 0 until Phase D; establish the independent timer core and pure hosted tests now.

## Internal interface and timing policy
A caller-owned static service stores a 64-bit count of elapsed IRQ ticks and a fixed positive ticks-per-second calibration. Initialization sets count zero, marks clock unavailable, and never waits on hardware. The first validated IRQ0 tick marks it ready. Read yields false without mutating output until ready. A tick increment is strictly bounded and saturates at UINT64_MAX rather than wrapping backward. Reader and ISR communicate through relaxed atomic tick operations and an acquire/release availability flag; future SMP needs its own contract.

Conversion to whole milliseconds uses quotient and remainder to avoid intermediate overflow and saturates at UINT64_MAX. With 100 Hz periodic delivery the resolution is 10 ms, plus scheduling and PIT divisor tolerance; this clock is only elapsed time since first successful tick, never civil date. It cannot distinguish lost IRQs or infer elapsed time before availability. The initial PIT input oscillator is 1,193,182 Hz; a reload of 11,932 gives approximately 100 ticks/s. Future tuning and calibration are separate work. The API remains kernel-internal and can later be backed by a different hardware source.

## Safety and validation
No allocations, sleeps, locks, callbacks, scheduling, or wall-clock reads in tick handling. Hosted tests cover nulls, uninitialized/unavailable, negative/rejected configuration, tick saturation, conversion at near UINT64_MAX, and elapsed-time arithmetic boundaries. QEMU tests in Phase C verify boot and inactivity with IRQs still masked; Phase D requires actual IRQ0 delivery plus measured tick rates, EOI and stall/storm checks. F12 remains on PIT channel 2 until a separately tested migration.

## Open concerns
PIT0 reprogramming and IRQ delivery are Phase D. Portability to APIC/HPET/TSC, saved/restored timer contexts, sleep APIs, preemption, scheduler and exact time source remain explicitly deferred.
