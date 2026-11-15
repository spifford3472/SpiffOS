# SpiffOS M10-D5.8c C1 — Real IRQ lock-owner frame acceptance

Date: 2026-11-14. Base: D5.8b PR #84 exact head 7d3cb42812f721dba71c6407b69c15ee06cb9dbf, which passed independent GitHub Actions run #83. Status: test-only C1 development; NOT complete C2 high-priority blocked waiter donation, and NOT owner ISO deployment.

## Design and integration

Governing ADR: ../decisions/ADR-m10-d58c-real-irq-lock-owner.md. The existing D5.5c/D5.6 zero-lock IRQ frame checks deliberately refuse any mutex owner and are left unchanged for all older and default builds. A separate M10-D5.8c C1 diagnostic flag, SPIFFOS_M10_D58C_LOCK_IRQ_TEST, combines M10-A exact RUNNING/RUNNABLE generation, D5.2b whole owner/resource ledger and D5.3 full lock graph via D5.8b with independent 20-qword IRQ frame stack bounds, saved RSP above frame, canonical RIP/RSP, saved CS/SS/RFLAGS/IF and observed live IF=0, critical depth=0. Supervisor continues using the established zero-ref frame checker.

The real QEMU fixture reuses D5.6's two independently owned CPU-bound x86 IRETQ worker stacks and reserved supervisor. Worker zero genuinely executes d53_acquire(rank 0) from its own task while RUNNING under nested guard-compatible IF=0, subsequently continues CPU-bound under real IRQ0 preemption, then calls d53_release off-IRQ after at least three observed hardware source interruptions while still owning rank 0. Worker one and the supervisor keep preempting through M10-A's sole IRQ handoff; D5.7 computes read-only priority selection. IRQ never mutates D5.3, memory owner or task references. The final boot verifier requires one acquire, one release, >=3 locked source IRQ frames, no outstanding lock, completed supervisor work and independently reaped worker stacks. Existing D5.5c and D5.6 strict helpers are not modified.

## Test and provenance

- Host test: make HOST_CC=gcc test-m10-d58c-frame; GCC C11 strict flags, ASan/UBSan; marker SPIFFOS_M10_D58C_IRQ_FRAME_GRAPH_OK. Positive actual aligned host stack+registered D5.3 rank-0 lock and 20-qword synthetic source; negatives bad IF/guard, mismatched CS/RFLAGS/RSP, bogus pointer, misaligned pointer, stale generation, forged counter and wrong lock owner. Input tables are unchanged on all refusals. Hosted synthetic frame is not itself hardware provenance.
- Real QEMU: make HOST_CC=gcc test-m10-d58c-lock-guest; flag-gated x86 IRQ0 / M10-A IRETQ execution. 128/256/512 MiB positives verify three hardware lock-held IRQ source frames, 1/1 acquire/release, two runnable CPU workers and supervisor max 3 ticks gap; no keyboard/editor regression. Separate target-frame, critical exclusion and stale-generation negative flagged QEMU builds are required and recorded by the harness.
- Full test-m10-d58c-acceptance includes the above and inherited D2–D5.8b. Exact-head GitHub CI, normal unflagged stripped ELF equality, owner-ISO rollback hash and final outcome must be recorded separately rather than assumed.

## QWEN and coordinator responsibilities

Qwen qwen3-coder:30b was invoked first for the smallest span bounds helper and returned a code fragment after 43 seconds. Review found an unsafe C overflow check using bottom+capacity and redundant pointer+bytes; coordinator did not copy it and instead used subtraction checks plus bottom>UINTPTR_MAX-capacity and bounded test coverage. Coordinator implemented source/target proof, live QEMU integration and tests. Codex not needed. No changes to physical disks, SpiffBridge, QTH networks or production OPNsense.

## Open C2/C3 gates and rollback

C1 proves live IRQ on a legitimate lock owner, not high-priority waiter BLOCKED/wake or dynamic donation, so do not call D5.8c complete. The D5.8b cooperative blocked context stores six callee-saved registers for RET; D5.6 uses a 20-qword IRETQ saved IRQ context. Safe interoperability, no IF-enabled window while BLOCKED, independent medium CPU worker, real donation selection, release/wake, graph/owner-death failure injections and later full production driver critical section audit remain open. No synthetic resume or forced unlock. Discard isolated branch/worktree to roll back; owner D4 ISO unaffected.

## Change history

2026-11-14: docs-first staged C1 design and first hosted/QEMU evidence.

## Verified C1 negative results and safety hashes

Final targeted commands ran in the isolated Forge source tree:
- make HOST_CC=gcc test-m10-d58c-frame: PASS, exact marker SPIFFOS_M10_D58C_IRQ_FRAME_GRAPH_OK.
- make HOST_CC=gcc test-m10-d58c-lock-guest: PASS positives on 128/256/512MiB and nine fail-closed QEMU negative boots (target frame mismatch, forced active critical section, stale target generation, each at 128/256/512MiB). Log: /tmp/spiffos-d58c-c1-negatives.log.
- Independent default build of build/spiffos-kernel.elf stripped with strip --strip-debug has SHA256 82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb; cmp confirms exact identity to the accepted D4 debug-stripped ELF, proving the new diagnostic is not linked into default build.
- Existing owner's Forge ISO remains SHA256 0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6 and was not modified.

Negative graph/orphan tests are present in hosted D5.8c and D5.8b; an *in-flight malformed source* under real IRQ0 still requires a dedicated safe-fault witness without inventing or running an unsafe source frame. The complete actual blocked-waiter donation/wake test remains C2, and owner approval is still required before any merge or deployment.
