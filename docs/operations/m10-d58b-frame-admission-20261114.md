# M10-D5.8b Phase 1 — Graph-backed frame eligibility and blocked-task contract

Date: 2026-11-14. Status: isolated test-only architecture and hosted proof. Base: feat/m10-d58-irq-mutex-integration, PR #83, exact parent 98c044a986b1b3feacaa00e38e3e31cfd092cc38. Owner D4 ISO unaffected.

## Rationale and implementation

The existing src/kernel/m10-d56-guest.c:frame_safe and src/kernel/m10-d55c-frame-owner.c:d55c_frame_matches correctly reject nonzero lock/waiter references, but cannot admit a legitimate D5.3 mutex owner. Directly removing those rejections would leave corrupt lock owners and blocked tasks eligible for IRQ return. This Phase 1 builds a separate, read-only metadata proof without touching the old x86 validators.

src/kernel/m10-d58b-admission.[ch] checks exact M10-A RUNNING/RUNNABLE generation, installed nonquarantined D5.2b stack owner, complete D5.3 graph equality for every lock/waiter, rejects any orphaned owner in any lock and refuses registered waiting tasks as IRQ destinations. It consumes injected IRQ/IF/guard witnesses, but does not authenticate hardware frame pointers or provenance, validate 20-qword saved-frame geometry, mutate state, allocate, or call any mutex function that changes state. A separate metadata-only d58b_blocked_departure_metadata accepts exact BLOCKED generation with M10-A current slot empty and a registered D5.3 wait reference under non-IRQ IF=0. It does not prove that CPU RSP has left the blocked stack; that mandatory x86 handoff is separate work.

## Validation

- make HOST_CC=gcc test-m10-d58b-admission: PASS, strict GCC C11 -Wall -Wextra -Werror -Wpedantic and ASan/UBSan, marker SPIFFOS_M10_D58B_LOCK_FRAME_METADATA_OK. The hosted fixture creates a real M10-A low-priority lock owner, a BLOCKED high-priority waiter and a competing medium task; checks metadata during donated priority and after release/wake, then rejects forged counts, stale handle/profile, changed owner/waiter generations, quarantined/overlapping owner stack, orphaned terminated owner and wrong IRQ/IF/guard state. Denial snapshots are byte-identical.
- Full D5.8b acceptance with 128/256/512MiB QEMU and inherited M9/M10 regression and exact-head GitHub Actions: separate gates, record actual status after execution.
- Unflagged normal debug-stripped ELF must remain identical to D4 SHA256 82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb.

## Model provenance, limitations and next test

Local QWEN qwen3-coder:30b was invoked first for a single state/zero-waiter predicate. It returned a correct C function in roughly 36 seconds. The coordinator reviewed and integrated the equivalent predicate, added whole-graph/orphan validation and authored the hosted fixture. No Codex used.

Next gate: implement a separately owned and independently witnessed non-IRQ IF=0 x86 CPU-stack departure after d53_acquire() returns BLOCKED. Only then consider adapting D5.6 saved-frame geometry validation to permit proven lock-bearing RUNNING/READY workers, retaining corrupt source/target and guard negatives. Do not enable production EDIT/BASIC preemption, IRQ-time D5.3 mutations, force-release locks or retry quarantine.

## Safety, rollback, change history

All work is in disposable Forge checkout /tmp/spiffos-d58b-phase1-20261114, with QEMU -net none and no physical drives. No network infrastructure or accepted ISO changes. Discard the feature branch to rollback. 2026-11-14: owner-directed Phase 1 design and hosted gate recorded.
