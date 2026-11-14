# ADR M10-D5.7 — Preempt-disable contract, donated-priority IRQ admission and fail-closed recovery

Status: owner elected M10-D5.7 isolated implementation on 2026-11-14. This is an incremental **test-only safety checkpoint**, not permission to enable preemption of BASIC, EDIT, drivers or the normal kernel. Governing AGENTS.md; accepted/open, system overview, developers guide; ADR-m10-d56-irq-handoff.md; ADR-m10-d53-bounded-synchronization.md; ADR-m10-d54-priority-dispatch.md; ADR-m10-d55-preemption-safety.md.

## Design and scope boundaries

M10-D5.6 proved two real non-yielding M10-A workers and a supervisor can switch at IRQ exit in QEMU. It uses a single global IF=0 critical counter, FIFO selection, and prevalidated frames, but this is not a production-wide critical-section system.

Work must be split into independently verified gates:

1. **D5.7a — strict preemption guard.** Add a fixed-capacity, single-CPU test-only guard that requires trusted IRQ/IF provenance and an exact current task generation for every enter/exit. Nesting must be LIFO and overflow/underflow/stale-handle/IRQ entry must latch a permanent failure; no attempted recovery by forced unlock or stack free. The IRQ selector is permitted to switch only at depth zero and only when its genuine interrupted task ownership is known. The guard is a diagnostic policy; it does not automatically instrument every real driver.
2. **D5.7b — priority-aware IRQ *selection*.** Reuse D5.3 donation and D1 bounded aging/supervisor reservation. Read D5.3 lock graph and M10-A ready queue under IRQ=0 without calling D5.3 mutating APIs inside IRQ. The selected exact READY handle is handed to the existing M10-A `kernel_task_irq_handoff` — no parallel lifecycle manager. Validate rollback on invalid frame, stale generation, quarantine or corrupt synchronization graph before any state mutation. Never ignore waiters or orphaned owners.
3. **D5.7c — failure containment.** A corrupt destination frame or stale identity must be rejected without altering the authoritative task table. A valid reserved supervisor context may receive control for diagnosis; no automatic lock-release, memory free or task resurrection inside IRQ. If current frame itself is unsafe, stop selecting new contexts and leave recovery to separately reviewed trap policy. Do not conceal an invalid source by guessing a fallback.
4. **QEMU acceptance.** Retain real 128/256/512MiB D5.6 guest, repeat with valid priority class selection and legitimate IRQ frame ownership. Inject a stale handle, corrupt frame and active guard; prove rejection before unsafe IRETQ; record actual serviced supervisor ticks. Hosted sanitizer tests must prove donated-priority outcomes, guard churn and unchanged state on all denial paths. Run inherited D2–D5.6 regression.
5. **Normal executable isolation.** New guest flags only; no normal editor/BASIC preemption, no physical disk, deployment or network changes; normal debug-stripped ELF hash must equal D4 baseline. Stacked draft PR, no automatic merge.

## Nonnegotiable technical distinctions

- A pure priority selector may run at the verified IRQ exit, but D5.3 acquire/release/cancel, resource mutation, allocation and cleanup remain **non-IRQ operations** with exclusion. Do not silently call D5.4's non-IRQ `d54_dispatch_next` inside the ISR.
- The diagnostic global guard cannot prove production-wide lock safety; all kernel locks/drivers, IRQ event queues, allocations and exact preempt-disable memory ordering require later targeted audits.
- Supervisor opportunities alone are not a physical hard realtime service bound.
- No self-healing, forced owner-death release, retrying quarantined stacks, Ring 3, CR3, SIMD or other-CPU preemption.

## Approval / history

2026-11-14: owner authorized investigation, implementation and tests of these safety boundaries; merge and physical release remain unapproved.


## Implementation and verified Forge acceptance — 2026-11-14

**Review:** draft PR #82 on `feat/m10-d57-preemption-safety`, stacked on #81 through #71. An isolated `m10-d57-safety.[ch]` policy introduces an exact-generation, fixed 8-level LIFO preemption guard with strict IF/IRQ input, per-depth release cookies and permanent latch on misuse; a bounded D5.3/D1 donated-priority IRQ read-only selector over the **existing** authoritative M10-A READY queue; and a post-commit age update that cannot manufacture lifecycle transitions. D5.3 mutex/ledger mutation continues to be prohibited from IRQ. Test-only `SPIFFOS_M10_D57_IRQ_TEST` integrates the read-only selection into the **actual D5.6 M10-A IRQ0 frame/IRETQ guest**, while nested IF=0 worker and supervisor regions use the guard. The supervisor disarms the selector before re-enabling IF following its termination. A corrupt D5.3 profile is injected only from the supervisor's own IF=0 safe point and rejected at the next IRQ, without attempting mutex repair or invalid frame selection.

**QWEN-first workflow:** Forge QWEN `qwen3-coder:30b` completed a narrowly scoped pure guard-admission helper in **31.22 seconds**. Coordinator reviewed/integrated its boolean rule with strict generation/cookie/LIFO fault handling and authored isolated policy/host/QEMU tests. No Codex was used.

### Actual testing

- `make HOST_CC=gcc test-m10-d57-safety`: **PASS** GCC `-Wall -Wextra -Werror -Wpedantic` with ASan/UBSan and marker `SPIFFOS_M10_D57_GUARD_DONATION_OK`. Tests exact generation/cookie, nested and out-of-order exit, overflow, IRQ entry, IF failure, sticky fault; actual D5.3 high-priority waiter blocking on a low-priority owner's lock with priority donation/read-only IRQ selection, donation revocation after non-IRQ release, unchanged state on invalid graph, and 20,000 supervisor-reserved priority decisions with fairness.
- `make HOST_CC=gcc test-m10-d57-guest`: **PASS in 46.98 seconds**, real QEMU 128/256/512MiB with **two non-yielding M10-A workers** using interactive and time-sensitive priorities (observed 21/99 worker IRQ slices each memory size), independently owned supervisor, preemption-guarded nested worker critical sections, and maximum **3 unserviced 100Hz IRQ ticks**, with normal editor/IRQ0 restored and off-stack cleanup. **Twelve of twelve** negative QEMU cases passed: invalid saved destination frame, active critical-depth, stale M10-A generation, and malformed D5.3 registered generation graph at 128/256/512 MiB. The malformed graph is introduced outside the ISR on a legitimate IF=0 supervisor stack.
- Full isolated `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine make HOST_CC=gcc test-m10-d57-acceptance`: **exit code 0 in 407.13 seconds**, disposable Forge tree `/tmp/spiffos-d57-20261114`, log `/tmp/spiffos-d57-acceptance.log`. All D5.7/D5.6/D5.5c/b/D5.5/D5.4/D5.3/D5.2d/c/b/a/D5.1/D4 complete/checkpoint/D3/D2 markers PASS, plus M9 keyboard/editor/workspaces, 100Hz IRQ0/negative, #UD/#DF, APIC/cache/alias/MMIO and fault-injection tests.
- Unflagged normal debug-stripped executable SHA-256 `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`, identical to established D4 unflagged baseline. Disposable normal ISO SHA-256 `d1463620b712a52eebc1d09a8752dc60ad0f3adae32f286b02292fb9f9c51b9c`; not accepted ISO provenance or byte-identity claim. The owner's normal Forge checkout `git status --short` empty. All tests used disposable QEMU, `-net none`, temporary OVMF state, no physical disk or production mutation.
- Final exact-head GitHub Actions remains a **separate** CI gate.

### Remaining gates / deliberately incomplete

The real-IRQ fixture proves priority-aware **base-class** scheduling with M10-A, and the hosted sanitizer proves D5.3 transitive donation in the presence of a blocked waiter. It does **not** yet prove combined real-IRQ multiworker lock contention and dynamic donation/wake, nor convert all runtime allocators, keyboard/editor state, resource registry, driver queues and interrupts to the preemption-guard API. All such production call sites require a separate read-only audit, smallest-reviewable instrumentations, actual stack/driver failure injection and owner approval. Owner-death and corrupt *source* context remain fail closed; no forced lock release or automatic reaper retries. No physical realtime guarantee, FPU/SIMD/CR3/Ring3 or SMP yet. No ordinary EDIT/BASIC preemption, merge or deployment was authorized.
