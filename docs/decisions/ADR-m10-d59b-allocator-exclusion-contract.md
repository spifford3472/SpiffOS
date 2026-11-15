# M10-D5.9-B — Allocator exclusion and ownership contract (design gate)

Status: **PROPOSED / docs-first** — 2026-11-15. No allocator runtime changes, no new preemption permission, no merge or deployment authority. Parent: PR #89 at `b4580c0e56d7e44266e70e93fecc375d14bf1e6d`.

## Evidence and scope

Read `AGENTS.md`, `docs/decisions/accepted.md`, `docs/decisions/open.md`, `docs/architecture/system-overview.md`, `docs/DEVELOPERS_GUIDE.md`, `docs/decisions/ADR-m10-d59-production-preemption-boundaries.md`, and `docs/operations/m10-d59-a-preemption-path-audit-20261114.md` before implementation. Existing D5.9-A audit covers 64 C/ASM sources by hash, but only 33 have explicit function inventory. The current ordinary editor is single-writer, and the physical frame, bounded heap and memory service interfaces require exclusive boot CPU/IF-off. The diagnostic D5.7 preemption guard is not a production allocator lock.

## Proposed ownership and execution policy

1. **Single CPU only.** SMP is unsupported. Until a verified allocator gate is integrated, existing allocation and release must remain on the exclusive boot CPU, with interrupts disabled, and must not be called by independently scheduled tasks.
2. **No allocation/free in IRQ, exception, interrupt-tail, or task-switch paths.** Such requests fail closed or are deferred through a bounded, explicitly owned off-IRQ queue; no implicit deferral or unbounded retry. No blocking mutex acquisition in IRQ.
3. **Whole-transaction protection.** Frame slot plus all owner/free counters, heap links plus split/coalesce/accounting, M6 root allocation/reset/publication, and associated rollback must be protected as complete transactions. Do not guard only individual stores, and do not invoke callbacks or perform blocking operations while protected.
4. **Interrupt state is inherited, not guessed.** A future gate must authenticate entry context (trusted CPU/IRQ provenance), save the original RFLAGS.IF exactly once at outermost entry, maintain bounded nesting for the same exact task generation, and restore the saved original IF only at the matching outermost exit. Never blindly STI. Reject mismatched owner, generation, CPU, underflow, overflow, or untrusted interrupt context without mutating guard ownership.
5. **The mutation owner is the exact task (slot + generation), not merely a numeric task slot.** Never permit a terminated slot reused with a new generation to release an old exclusion token. The currently boot-only owner must have a specifically documented identity until true task contexts exist.
6. **Protected operations must be bounded.** Measure worst-case IRQ-off latency in hosted instrumented tests and QEMU before admitting any worker use; do not claim a bound based only on a source review. Long payload zeroing, whole-heap validation, and workspace reset must be assessed separately; do not assume they belong in an IRQ-off critical region.
7. **No early stack reclamation.** Active, referenced, blocked, or suspended task stacks cannot be released; deferred cleanup must be performed by a verified off-stack owner only after absence of scheduler, IRQ, and synchronization references is proven. Orphaned lock ownership is quarantined, not automatically transferred or cleared.
8. **Failure atomicity.** Validate before mutation wherever possible. On OOM, invalid owner, exhausted metadata, failed frame access, and partial mapping, preserve or restore all metadata, accounting and root pointers. If rollback cannot be proved, quarantine affected storage and report a recoverable error rather than publishing inconsistent data.
9. **Read-side contract.** Integrity checks and inspection cannot run concurrently with mutations unless they hold the same gate or read an explicitly coherent snapshot. The F10 diagnostic must use bounded snapshots rather than traverse live heap links from an IRQ handler.
10. **Boot compatibility.** Default unflagged build and owner accepted ISO remain unchanged until an independently accepted, separately flagged preview passes regression and exact-head CI.

## Detailed mutation inventory (not yet certified)

| Service | Mutation transaction | Existing source | Required review |
|---|---|---|---|
| Frame allocator | slot owner, owned count, free count, reserve accounting | `src/kernel/frame-allocate.c`, `src/kernel/frame-allocator.c` | validate counters and reserve policy at every failure injection point |
| Bounded heap | list split/coalesce, active links, allocation owner, bytes and counts, payload zeroing | `src/kernel/kernel-heap.c` | preserve graph invariants and verify bounds/latency |
| M6 memory service | root pointers, workspace/session state, owner accounting, reset and retirement | `src/kernel/memory-service.c` | publication atomicity and cross-workspace isolation |
| Page service | frame selection, mapping/zeroing and failure unwind | `src/kernel/page-access.c` | page mapping/rollback and no active-stack free |
| MMIO mappings | page-table/driver map staging and teardown | `src/kernel/x86-mmio-map.c` | separately gate MMIO serialization under D5.9-D |
| Tasks and resources | generation-bound allocation, references, deferred release | `src/kernel/kernel-task.c`, `src/kernel/kernel-task-resources.c` | stale generation, in-use stack and off-stack cleanup |

## Smallest first implementation assignment (do not expand scope)

Implement a **hosted, isolated exclusion-gate state machine** and its tests, behind an opt-in diagnostic target only. No existing allocator API calls the gate yet. Define a small interface whose enter/exit operations accept a trusted, non-IRQ execution-context witness and an exact-generation owner token. The caller provides a mockable read/disable/restore-IF backend; ordinary tests must not execute privileged CLI/STI. Use explicit results for invalid context, ownership conflict, nesting overflow, and invalid release. Do not permit task switching while a transaction gate is owned.

Test first: entry with IF on/off; nested matching entry; mismatched task and generation; double exit; spoofed IRQ witness; stale owner; counter limit; mutation-point fault injection; retry after failure; no partial changes on invalid admission. Then integrate just the frame-slot/counter transaction in a **separately flagged** test fixture, preserving the production ABI and enforcing the D5.9-A source-manifest drift gate. Only after that passes, consider heap, memory service and page paths individually.

## Acceptance and stop conditions

- Demonstrate deterministic negative failures with no metadata change; run ASan/UBSan hosted tests and QEMU at 128/256/512 MiB.
- Confirm actual IF state, owned stack generation, nesting semantics, no allocator execution in IRQ, no orphan release, and bounded critical-section duration in a real IRQ experiment.
- Run `python3 scripts/audit-m10-d59.py`, `python3 tests/m10-d59-audit-test.py`, full inherited acceptance, keyboard/editor/F1–F12 regression and exact-head GitHub CI.
- Verify unchanged default unflagged kernel artifact against the accepted D4 ELF SHA `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`. Preserve accepted ISO SHA `0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`.
- Reject unsatisfied provenance or unsafe cross-context use; do not silently allow normal editor/BASIC preemption. Merge and deployment remain subject to separate authorization.

## Change history

- 2026-11-15: proposed documented gate contract and bounded first coding/test assignment. No code wired or runtime safety certification.
