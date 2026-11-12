# M9-3A exception-stack validation — 2026-11-11

## Status
Phase A candidate verified in isolated Forge QEMU source snapshot based on `f96ba582ecfa0ef913c0fe7cd2793ab962eb8375`. **Not a complete M9-3 release.** No owner ISO, Linux installation, bootloader, hardware partition, network, or OPNsense modification.

## Architecture and review
Read root AGENTS.md, accepted/open decision registers, system overview, Developers Guide, M9 foundational/exception/memory decisions, frame-allocator/memory services, M7/M8, ADR-011 and earlier M9 operation records. Local QWEN 30B design pass recommended a kernel-owned GDT with TSS and IST1; coordinator refined the design to copy the currently active GDT's descriptors without changing selectors. See ADR-m9-3-exception-stacks.md. No Codex delegation.

## Files and interfaces
* `src/kernel/x86-tss.h/.c`: 104-byte TSS, 16-byte 64-bit TSS descriptor encoding, GDT preservation, boot CPU LTR/STR verification, 16 KiB dedicated IST stack.
* `src/kernel/x86-idt.c`: install TSS first, then assign vector 8 to IST1; other exceptions retain normal stacks and external interrupts remain absent.
* `src/kernel/main.c`: QEMU-only destructive-stack diagnostic behind compile-time SPIFFOS_DOUBLE_FAULT_TEST.
* `tests/x86-tss-test.c` and `scripts/test-double-fault.py`: negative descriptor and actual double-fault tests.
* `Makefile`: freestanding TSS object and new test targets.
* Documentation registers, architecture overview, developers guide, and Phase A ADR updated.

## Executed tests
1. Unchanged M9-2 baseline: `make test-idt-memory` PASS.
2. Baseline `make test-memory` PASS after explicitly binding the pinned local Limine dependency into an isolated source snapshot and using a temporary per-process Git safe-directory config (no global trust edits). Real QEMU 128/256/512 MiB, screenshots and serial counters PASS.
3. Candidate freestanding Clang build: PASS.
4. Candidate `scripts/test-memory.py`: PASS at 128, 256, 512 MiB with exact framebuffer verification.
5. Candidate `make test-idt-memory test-idt-fault test-double-fault`: PASS. `#UD` vector 06 and deliberate poisoned-stack `#DF` vector 08 both emitted exactly one fatal exception report and did not reach normal boot.
6. Candidate `make test-memory test-workspace`: PASS hosted memory, logo, framebuffer, scrolling, exact QEMU framebuffer and serial checks across 128/256/512 MiB.
7. Candidate `make test-keyboard`: PASS including editor symbols/Insert/Delete, exact framebuffer comparisons, nine independent F1–F9 workspaces, F10 system view, and measured F12 indicator duration **1.007 seconds**.

## Deployment and rollback
Candidate ISO is isolated at `/tmp/spiffos-m9-3-rtremote/build/spiffos.iso`, SHA-256 `bfee7996b6c89562f25f12d6799ccd59ad358dae89e8e293bd70b959e861c5a4` (checksum before final evidence/report changes; verify again before use). It is not installed into the owner's checkout. Owner's known-good `/home/geoff/projects/spiffos-qemu-main/build/spiffos.iso.pre-m9-idt` remains untouched. To rollback any later owner-installed ISO, stop its QEMU instance and replace the candidate with the independently verified known-good backup; never manipulate physical disks.

## Limits and next work
The dedicated stack has no mapped guard pages. Double-fault handler is fatal rather than recovering. No APs/SMP, IRQ or preemptive scheduler, APIC/PIC remapping, device service registry, serial IRQ, keyboard IRQ or timer interrupt. Complete Phase B through E separately and run full tests at each checkpoint. No new LAN/OPNsense infrastructure configuration was needed.
