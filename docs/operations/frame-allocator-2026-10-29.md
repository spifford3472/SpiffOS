# M2 frame allocator — 2026-10-29

## Scope and authority
The owner authorized memory foundations, very small Qwen jobs with Codex
fallback, and local QEMU merges/deployments until 2026-10-30 03:00
America/New_York (07:00 UTC). Docs-first commit fe1a1ed records M2 scope.
References read: AGENTS.md, accepted/open decision registers, system overview,
ADR-memory-foundation, ADR-frame-allocator, ADR-011, ADR-002 and ADR-008.
No physical-media, disk, firmware, network or infrastructure change.

M2 implements caller-owned physical-page accounting with kernel/session/nine
workspace/supervisor tokens, explicit errors, protected reserve and ownership
checks. Returned addresses are numbers; no candidate frame is read or written.
There is no heap, page-table change, user process, actual workspace manager,
concurrency support or hardware isolation. Borrowed immutable metadata and
exclusive ownership are required. Stale same-owner handles need a future API.
The diagnostic uses 8MiB kernel BSS for up to 16GiB candidate RAM and 64 reserve
pages; capacity beyond the whole-map table reports UNAVAILABLE without capping
RAM silently. Final metadata strategy/reserve sizing remain open.

## Model provenance
Qwen received one bounded job: implement frame_allocate only, reading AGENTS.md,
the focused M2 ADR and supplied header. It hit its 300-second wall-clock budget
without producing the function (build/frame-allocate-qwen.log). The coordinator
used the owner's direct/Codex memory fallback authorization for the allocator
helpers, the missing allocation function, host tests, boot probe, QEMU verifier
and documentation. Qwen did not complete an M2 implementation. The existing M1
Qwen attempts/fallback are recorded separately in the memory inventory record.

## Verified results before merge
make test-frame-allocator passed with ASan/UBSan: unsorted maps and holes,
frame zero/alignment, capacity and reserve rejection without mutation, every
owner, both eligible pools exhausted, forbidden reserve access, rejected wrong
owner/double/bad frees, reuse, counters, guards and upper-address arithmetic.
make test-memory test-keyboard test-workspace test-workspace-scroll completed
exit 0 in 38.04 seconds. Both M1 adapter tests and M2 allocator tests passed.
Strict freestanding compilation/link and all existing editor/framebuffer
expectations passed, including block/underscore and push-down/shrink reflow.
python3 -m py_compile scripts/test-memory.py and git diff --check passed.

| QEMU RAM MiB | Candidate frames | Ordinary free after probe | Reserve free |
|---|---:|---:|---:|
| 128 | 18940 | 18876 | 64 |
| 256 | 51708 | 51644 | 64 |
| 512 | 117244 | 117180 | 64 |

All guests produced MEMORY_OK, ALLOCATOR_OK, BOOT_OK and KEYBOARD_READY.
Independent Python recomputation matched the complete region map and pool
counters. All probe allocations were released; no live owners remained.
The extra kernel BSS reduces usable counts from M1; it is not leaked allocation.
Evidence: build/m2-validation.log, memory-inventory-results.json and
memory-{128,256,512}-serial.log/run.log in the feature worktree.

## Launcher and deployment
python3 scripts/test-safe-launcher.py passed, exit 0 in 26.61 seconds:
real boot, absolute pointer, editor screenshots, lock rejection, QMP close and
SIGTERM child/private-resource cleanup. Evidence: build/m2-launcher.log.
Manual GTK resize/focus/release/close remains a separate acceptance item.
Merge/deployment results will be recorded after actual completion. Preserve the owner's running QEMU and its lock. Rollback is an
isolated build of M1 merge 81d457b. No history rewrite or owner-index changes.
