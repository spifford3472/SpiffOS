# M10-D4 integrated diagnostic scheduling — validation and open gates

Status: **isolated QEMU implementation and staged review**, no merge,
installation or physical media changes. Owner requested D3.1 then
continuation into D4, but did not grant a new D3.1/D4 production
merge/deployment. Installed Forge SpiffOS remains accepted M10-D3.

Governing: `AGENTS.md`, `docs/decisions/accepted.md`,
`docs/decisions/open.md`, `docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-task-scheduler-foundation.md`,
`docs/decisions/ADR-m10-d-preemption-proposal.md`,
`docs/decisions/ADR-m10-d31-workspace-diagnostics.md`,
`docs/decisions/ADR-m10-d4-priority-supervisor.md`,
`docs/DEVELOPERS_GUIDE.md`.

## Implemented checkpoints

1. **D4.1 (two worker phase test):** A permanent boot supervisor
   maps to priority policy slot 15; worker A cycles Background/Normal/
   Interactive/Time-sensitive in four 80-tick epochs, competing against
   a Time-sensitive CPU hog. Age+priority score is chosen by the tested
   D1 bounded selector in the test-only IRQ-exit path, after the normal
   PIT0 handler and one LAPIC EOI. Every fourth dispatch reserves
   supervisor service; ages saturate, and actual cross-stack changes
   are counted separately from self-selection.
2. **D4.2 (four simultaneous classes):** With a special test-build flag,
   the M10-B owned stack fixture expands from 2 to 4 independently
   mapped 16 KiB contiguous four-page kernel stacks. Workers A/B/C/D
   are all non-yielding and concurrently READY with Background,
   Normal, Interactive and Time-sensitive priorities. Worker
   progress, worst wait <64 dispatches, supervisor >=100 reserved
   dispatches in a 400+ tick run, full trap frames, 64-byte bottom
   canaries, cleanup and original boot-stack return are checked.
   Normal/D3/D4.1 builds still use the two-stack fixture.
3. **D4.3 (synthetic block/wakeup):** A and C are excluded from READY
   candidates during ticks [100,140) and [210,250) respectively.
   Their CPU progress must freeze after initial suspension. When
   made READY again, their wait age restarts at zero and they must
   resume within 64 PIT0 interrupts. A never-wake negative and
   deliberately impossible blocked-worker progress must fail closed
   with no successful boot/editor marker. The stack stays owned and
   mapped throughout and is reclaimed only after return to boot.
4. **Partial stack allocation failure:** A special four-stack
   negative image refuses stack #3 after two fully owned stacks have
   been allocated. The creation path releases both acquired owners
   in reverse order; a failure in release uses a separate poisoned
   diagnostic outcome. No IRQ selector is published on failure.

## Evidence (independently executed)

- D4.1 exact-source full `make HOST_CC=gcc test-m10-d4-acceptance`:
  PASS, markers `SPIFFOS_M10_D4_ACCEPTANCE_OK`; see
  `docs/operations/m10-d4-priority-validation-20261113.md`.
- D4.2 initial `make test-m10-d4-four-class`: 12 QEMU
  positive/negative guest cases PASS at 128/256/512MiB, including
  four-stack CPU hogs, invalid priority, disabled IRQ0 and corrupt
  canary; exact code SHA
  `e5d78fef1de087117294a6ecfddcde7aca9f07e5`.
- D4.2 combined `make HOST_CC=gcc test-m10-d4-acceptance`
  PASS in ~250.96s at exact SHA above; marker
  `FOUR_CLASS_COMPLETE_REGRESSION_PASS`;
  log `/tmp/spiffos-m10-d4-four-full.log`.
- D4.3 initial `make test-m10-d4-block-wakeup`: nine real
  QEMU cases PASS at 128/256/512MiB (three wakes, three never-wake,
  three illegal blocked progress) at SHA
  `cfc646ca887ec42b31da3e8a0f31c6884708b2fb`,
  log `/tmp/spiffos-m10-d4-block-first.log`.
- The full suite for the D4.3 initial code and the subsequent exact
  final SHA (including the new third-stack allocation failure)
  will be recorded below only after they have actually finished.
  No unexecuted tests are represented as passed.

## Review notes

Local QWEN `qwen3-coder:30b` performed focused reviews of the
two-to-four-stack expansion and synthetic READY-mask block/wakeup
design. The coordinator implemented small reviewable C and test
changes and validated strict freestanding compile and real QEMU
behavior. No Codex escalation. Deliberate malformed enum and counter
injections appear **only** under explicit negative test flags.

## Remaining D4/D5 boundaries

The block/wakeup fixture is a synthetic read-only READY mask, not
the final kernel user-task blocked/wakeup API. D4 still needs
owner-reviewed process/task state transition integration, supervisor
recovery under heavier allocator/stack exhaustion and broader
worst-case latency/quantum measurements. D5 generalized preemptive
editor/kernel driver concurrency, ring-3 user ABI, SMP, SIMD and
CR3 handling are not addressed.

## Safe rollout and rollback

The production ordinary kernel/ISO never links the test-only
M10-D1/D2/D3/D4 runtime objects without diagnostic build flags;
source tests use disposable QEMU ISOs and `-net none`.
Forge owner's normal ISO was
`4fcda82804c72fb4697175b811865755deb3caa4605797bce7a1d7c521eda596`
at accepted M10-D3. No owner checkout, disk, network, firewall or
permissions changes are made here. Rollback is deletion of the
temporary review checkout/builds; existing M10-D3, M10-A–C and M9
owner rollbacks remain intact.
