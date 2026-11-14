# SpiffOS M10-D5.8a — Deferred IRQ observation checkpoint

Date: 2026-11-14. Branch: `feat/m10-d58-irq-mutex-integration`, stacked on D5.7 commit `71a8c6c140866651ae63d6a8368bc800a3b76773`. Status: **isolated test-only development; no merge or deployment authorization**.

## Why this checkpoint exists

D5.7 demonstrated genuine priority-aware IRQ handoff but did not connect live lock contention/donation/wake. Before touching D5.3 while tasks are genuinely preempted, introduce and independently test a bounded observation path that cannot be confused with permission to run D5.3 mutation inside IRQ. This helper does not solve D5.8b contention or production-wide synchronization.

## Implementation

- `m10-d58-notify.[ch]`: fixed eight-element, stack/pointer-free ring of exact M10-A task generation and observed tick. Only validated RUNNING source on genuine IRQ exit with IF cleared may enqueue. Overflow, identity mismatch, invalid provenance, rollback tick, stale generation or unsafe supervisor call latches permanent failure. No dynamic allocation, task transitions, D5.3 operations or hardware calls. Consumer runs only at a separately verified IF=0 supervisor safe point and revalidates generation/state before drain.
- `m10-d56-guest.c`: under new `SPIFFOS_M10_D58_NOTIFY_TEST` *and* existing D56/D57 flags, authentic IRQ0 code samples live IF and records the M10-A RUNNING source; the already verified supervisor stack drains notices outside IRQ. Success requires every recorded IRQ to be drained, live worker/supervisor counters, valid task/resources and no queue fault. Normal unflagged kernel does not link this code.
- Hosted sanitizer fixture injects stale/forged generations, wrong IRQ/IF, empty/full queue behavior, wrong consumer context, stale termination and non-monotonic ticks, proving no M10-A state mutation. Real QEMU guest checks all three RAM sizes with `-net none`, temporary OVMF and no physical drives. CI selects `test-m10-d58-acceptance` when present.

## Model contributions and review

The locally installed QWEN `qwen3-coder:30b` was invoked first for the smallest possible pure identity/provenance predicate; it returned a correct boolean rule in ~33 seconds. The coordinator reviewed, folded the conditions into `current_matches` and IRQ admission rather than introducing redundant policy authority, then implemented the bounded queue, tests and flagged QEMU wiring. No Codex was used. A test harness initially expected D56 success marker before D58 metric, but actual serial prints metrics first. The assertion order was corrected and rerun; no kernel failure occurred. A review also tightened monotonic-tick validation after queue empty and replaced a guest-supplied constant IF witness with independent `pushfq` observation.

## Validation (actual local Forge evidence)

- `make HOST_CC=gcc test-m10-d58-notify`: PASS, GCC `-Wall -Wextra -Werror -Wpedantic`, ASan/UBSan, marker `SPIFFOS_M10_D58_DEFERRED_NOTIFICATION_OK`.
- `make HOST_CC=gcc test-m10-d57-safety`: PASS, marker `SPIFFOS_M10_D57_GUARD_DONATION_OK`.
- `make HOST_CC=gcc test-m10-d58-guest`: PASS in disposable QEMU at **128/256/512MiB** after independent live-IF read. Each run: **220 real IRQ observations and 220 drained outside IRQ**; worker IRQ slices **21/99**, maximum unserviced supervisor gap **3 ticks**, both normal IRQ0 and editor/keyboard readiness preserved. Log: `/tmp/spiffos-d58-guest-validated-if.log`.
- Inherited `test-m10-d57-acceptance` command launched in a separate Forge checkout with full M9/M10 tests; final exit and details must be recorded separately before release review.
- Normal unflagged ELF independently built in `/tmp/spiffos-d58-normal-check-20261114`, `strip --strip-debug` SHA256 `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`; verified byte-identical (`cmp`) to the accepted D4/D5.7 normal-stripped reference. The final D58 comprehensive acceptance and exact-head GitHub Actions conclusion remain separate gates.

## Scope, rollback and follow-up

No owner QEMU ISO installation, physical media, network, OPNsense, host boot or production editor changes. Owner accepted D4 ISO stays at `/home/geoff/projects/spiffos-qemu-main/build/spiffos.iso` (observed SHA256 `0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`). The disposable source workspace is `/tmp/spiffos-d58-20261114`; discard branch/workspace to roll back. Push only an isolated draft for review; do not merge or deploy without explicit owner approval. Next D5.8b requires revising diagnostic frame-resource admission: D5.6 currently rejects any task frame with nonzero `owned_locks` or `registered_waiters`, correctly preventing an unreviewed live lock-owner switch. A separate approved frame/ledger invariant and genuine blocked CPU-stack handoff must precede any QEMU contention test. Do not weaken the D5.6 safety check simply to make a worker with a lock schedulable.

## Change history

2026-11-14: first docs-first D5.8a implementation and hosted/real-QEMU evidence; full integration intentionally OPEN.
