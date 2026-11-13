# M10-A–C Engineering Evidence — 2026-11-12

Status: ISOLATED REVIEW CANDIDATES; not merged, not owner-deployed.
Authorization: owner approved M10-A/B/C implementation, M10-D requires a
separate explicit acceptance. No authorization inferred for merger/deployment.
Architecture: docs/decisions/ADR-m10-task-scheduler-foundation.md.

## Source and test environment
M9-3D6 accepted main: 6b4dffbaab20d6717ccb8ea2d85d84e7c0621a62.
M10-C tested source checkpoint before documentation-only commits:
44fe3ec86925e1545246ebccdfb2bd0975317cf7.
Isolated Forge source: /tmp/spiffos-m10c-review-fOoa6o.
All QEMU images used private writable OVMF variables, -net none and no
physical disks. Pinned Limine dependency resolved read-only from the
owner's validated installation. Original Forge checkout stayed clean.
M10-C default ISO checksum at that checkpoint:
e3d070049f4ea506ac8dd94e0c795525189c184e10a81b012e492b7269c05461.

WARNING: The owner's previously installed ISO at Forge path
/home/geoff/projects/spiffos-qemu-main/build/spiffos.iso hashed
758492d3b4aea5587e28514e5882231929e25a90f31c7ce1a38bae8135eb4ba2
during read-only audit, not the previously recorded accepted M9-3D6 test
artifact SHA256
06856f9311f7894d5b870add2afd0b457309853433a689e4c74f1f11aa0b5a32.
Do not replace either artifact until provenance and release gates are
resolved. Keep the known-good ISO available and checksum verified.

## M10-A
- Host GCC with ASan/UBSan: PASS SPIFFOS_M10_A_POLICY_OK.
- Freestanding Clang C compilation: PASS.
- Hosted cases: empty/single/multiple task queue, FIFO fairness,
  illegal state transitions, opaque generation reuse, supervisor reserved
  slot, table exhaustion, retired handle rejection, generation overflow.
- make HOST_CC=gcc test-memory: PASS (M9 memory and exact frame pixels at
  128/256/512 MiB), along with M10-A policy tests.
- Clang host sanitizer runtime unavailable; GCC sanitizer used.
- No IRQ, stack switching or kernel executable task launch claimed by A.

## M10-B
- Native two-stack switch test: PASS SPIFFOS_M10_B_HOST_SWITCH_OK.
- Test-only QEMU switch candidate: PASS 128/256/512 MiB.
- M10-A FIFO dispatch, 512 yields per task, first-run stack
  trampolines, task exits and switch back to boot execution.
- Eight ordinary M2/M3 KERNEL frames reserved for two 16 KiB stacks,
  checked physical and HHDM contiguity and release from boot stack.
  Supervisor reserve untouched.
- Reproducible make test-m10-b-guest: PASS at all three RAM sizes,
  including subsequent retained-memory and keyboard READY validation.
- An initial test assumption demanded the clock before its first real
  IRQ; fixed and rerun. A compile-only portability error and a
  diagnostic duplicate definition were fixed before successful tests.
- Limitations: no ring-3 isolation, guard pages, production task service,
  independent manual register sentinel proof, or fault-injected stack
  allocation rollback evidence. M10-B remains a draft acceptance candidate.

## M10-C
- Host GCC with ASan/UBSan: PASS SPIFFOS_M10_C_ACCOUNTING_OK.
- Freestanding Clang default and M10-C test ISO builds: PASS after
  correcting an omitted linker object in the first candidate.
- make test-m10-c-guest: PASS QEMU at 128/256/512 MiB: IRQ0 delivers
  ticks; pending bit is consumed in polled keyboard loop safe point,
  without an interrupt-context context switch.
- Default kernel only records a pending request, does not consume or
  schedule tasks.
- make HOST_CC=gcc test-memory: PASS, exact QEMU framebuffer and
  memory metrics at all 3 supported sizes.
- make HOST_CC=gcc test-keyboard: PASS, including character, editor,
  reflow and function-key serial evidence.
- make test-idt-fault: PASS genuine #UD exception and fatal halt.
- make test-double-fault: PASS poisoned-stack #DF on dedicated IST1.
- make HOST_CC=gcc test-irq0: PASS hardware 100-tick intervals
  0.994, 0.994, 0.995 seconds at 128, 256, 512 MiB respectively;
  disabled IRQ0 negative route: PASS, IF=0, clock unavailable.
- Existing M9 PIT0 configuration, IRQ vector 0x30, PIC masks and LAPIC
  EOI ownership unchanged. M10-D preemption unimplemented.

## Agent review and corrections
Local qwen3-coder:30b consulted for narrow A, B and C architecture
hazards. Reviewer incorrectly proposed phase-A 10ms preemption and u64
wrapping and questioned the proven ABI stack alignment; coordinating
architect rejected those points and retained the documented M10-D gate.
Local QWEN did not author the reviewed code. All accepted changes were
independently compiled and tested as described above.

## Follow-up release gates
1. Independent callee-saved register sentinel test and negative
   stack-frame/mapping failure injection for M10-B.
2. Review code comments, no-red-zone/stack alignment and saved-IF
   critical-section conventions.
3. Review exact GitHub source commit and reproducibly built ISO,
   checksum it, verify full test matrix after final source changes.
4. Owner review of draft PRs and explicit merge/deploy authorization.
5. Before any local ISO replacement, preserve and verify existing
   working image, ensure clean fast-forward Git worktree, and prepare
   rollback to M9-3D6.
6. M10-D requires a NEW architecture and owner acceptance; no
   timer preemption allowed in M10-A/B/C.

## Change history
2026-11-12: initial M10 architecture, A scheduler, B cooperative switch,
C timer accounting and isolated regression evidence recorded. No QTH
network, OPNsense, hardware or production service changes were made.
