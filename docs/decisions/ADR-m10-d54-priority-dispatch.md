# ADR M10-D5.4 — Authoritative effective-priority dispatch and QEMU multiwaiter proof

Status: owner-directed **isolated implementation** (2026-11-13), stacked on draft PR #76 and previous D5 branches. Governing: AGENTS.md; accepted/open decisions; architecture overview; ADR-m10-task-scheduler-foundation; ADR-m10-d4-priority-supervisor-proof; ADR-m10-d5-production-task-model; ADR-m10-d53-bounded-synchronization; DEVELOPERS_GUIDE.

## Context
D5.3 computes transitive priority donation but M10-A always consumes the FIFO head. D4's diagnostic priority controller must remain separate. D5.2d's two-worker QEMU test does not prove D5.3's *multiwaiter* lock execution. We must not introduce a second lifecycle state owner.

## Decision
Add an **exact-handle selected dispatch operation inside the existing M10-A table**. It applies the same single RUNNING transition, removes exactly one known RUNNABLE entry, validates the whole table and full handle/generation before any mutation. Existing FIFO API remains unchanged for existing tests. A separate bounded *selection adapter* builds candidates solely from M10-A's ready queue and the D5.3 generation-tagged priority/lock ledger; it computes effective priorities, waits/age and supervisor reservation, then invokes the M10-A selected dispatch. Neither adapter duplicates task states, context ownership, locking or stacks.

The supervisor has exclusive slot 15, never enters D5.3 ordinary lock graph, is always selected at least every fourth dispatch **when RUNNABLE**. If not runnable, there is no invented supervisor thread or forced dispatch. Selection is single boot CPU, IF=0, non-IRQ, no allocation. Keep deterministic starvation mitigation through bounded ready-age score; no guarantee of CPU-time realtime deadlines or IRQ preemptive scheduling. Reject missing/stale generation profiles and inconsistent registries without changing task state, age or output. Priority donation is recomputed from the registered lock graph so it disappears on unlock/cancellation.

A separately flagged M10-B test-only QEMU cooperative fixture must demonstrate >=3 independently stacked workers: owner and two blocked waiters, donor ordering, transferred ownership/wakeup, resuming the *actual* saved C continuation after switch-away, and safe final memory release from surviving dispatcher. Preserve established two-worker regression and normal editor binary.

## Safety acceptance
Host GCC ASan/UBSan: selected-dispatch identities, corruption and no mutation, supervisor reserve under sustained load, donated priority selection and revocation, tie fairness, starvation bounds and 20,000 generation reuse. Real QEMU 128/256/512 MiB multiwaiter worker switch tests plus inherited M10/D4/M9 full acceptance and negative cases; default ELF hash comparison. Keep isolated PR draft and exact-head CI; do not merge, deploy accepted ISO, touch physical drives, enable normal editor preemption, Ring 3 or network. D5.3 orphan locks still fail closed.

## Open
Supervisory service measured in *dispatches*, not hard CPU time; IRQ-preemptive lock safety, actual kernel user task ABI, stack/interrupt race handling, nested lock recovery and owner-death protocol require later approval.

## History
2026-11-13: owner selected D5.4 following D5.3; documentation-first scope approved for isolated implementation, not release.


## Implementation and verified isolated validation — 2026-11-13

Draft PR #77 on `feat/m10-d54-priority-dispatch`, stacked on draft PR #76 (D5.3) through earlier #71. `kernel_task_dispatch_selected()` validates the exact full `(slot,generation)` identity and retains M10-A as the only RUNNING/ready queue state mutator. `kernel-dispatch-d54.[ch]` is a bounded selection/age adapter: uses D5.3 effective priority (including transitive donation), existing D1 `kernel_priority_choose` and one-of-four dispatch opportunities for a runnable supervisor in reserved slot 15. This is a *dispatch opportunity*, not a CPU-time deadline.

`m10-d54-guest-smoke.c` is a **new test-only three-stack fixture** (`SPIFFOS_M10_D54_GUEST_TEST`), separate from existing D5.2d two-stack execution checks. Its background owner locks rank 0 and blocks on a modeled external event; a time-sensitive and interactive waiter block on the mutex on their own physical kernel stacks. The surviving dispatcher wakes the owner, which receives donated priority and transfers the mutex to high waiter, then medium waiter. Both resume their independently saved x86-64 C continuations and release ownership in turn. All three task descriptors terminate; boot dispatcher uses the existing bounded supervisor cleanup queue to release exact memory-owner backing after each worker has switched off-stack. Canary and registered lock/wait counts verified. Owner/waiter phase ordering, editor and IRQ0 checked before guest reports success.

### Evidence on Forge disposable checkout

- QWEN `qwen3-coder:30b` was invoked **first** with a small exact-generation ready-queue helper problem and returned code using nonexistent M10-A identifiers, including `M10_A_READY_QUEUE_SIZE` and `M10_A_INVALID_SLOT`. Coordinator rejected the incorrect snippet and implemented selected dispatch against actual inspected M10-A APIs. No Codex used.
- `make HOST_CC=gcc test-m10-d54-dispatch`: **PASS** under GCC ASan/UBSan and `-Werror`, marker `SPIFFOS_M10_D54_PRIORITY_DISPATCH_OK`. Tests effective donated-priority selection and revocation, stale-generation immutability, bounded aging and **20,000** runnable-supervisor dispatch cycles with no interval over four decisions.
- `make HOST_CC=gcc test-m10-d54-guest`: **PASS** for real QEMU 128, 256, 512 MiB; three owner-backed stacks, two actual blocked mutex waiters, both saved-continuation resumes, final off-stack cleanup, restored editor and IRQ0. An initial guest expected FIFO order immediately after high waiter blocked; real donated priority correctly selected owner instead. The fixture was corrected to use a legitimately externally blocked owner, with subsequent QEMU PASS at all three sizes. No kernel scheduler policy was weakened to accommodate the test.
- Full inherited `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine make HOST_CC=gcc test-m10-d54-acceptance`: **exit 0 in 273.78 seconds** from `/tmp/spiffos-d54-20261113`, log `/tmp/spiffos-d54-acceptance.log`; new `SPIFFOS_M10_D54_ACCEPTANCE_OK` and inherited D5.3, D5.2d, D5.2c, D5.2b, D5.2a, D5.1, D4-complete/checkpoint, D3 and D2 markers all present. M9 keyboard/editor/workspace tests, real QEMU IRQ0 positive/disabled, #UD/#DF exception handling, APIC and MMIO positive/negative tests PASS.
- Normal unflagged debug-stripped kernel ELF SHA-256:
  `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`, **identical to established D4 normal executable baseline**. Disposable normal ISO SHA-256:
  `b70e11b67ee1ee7358d56e8230724ea647f146dc659e18f48b1ffc4ffa22d259`, not an accepted owner artifact or ISO provenance equivalence claim.
- QEMU used `-net none`, transient OVMF variables, no physical disks and no accepted ISO replacement. Owner's normal Forge checkout `git status --short` remained empty.

Remaining checks: exact final-head GitHub Actions success and owner review before merge/deployment. This proof is *cooperative and test-only*, not a validated IRQ-preemptive kernel mutex, scheduling CPU-time guarantee, preemptible editor, Ring 3 ABI or general dead-owner recovery.
