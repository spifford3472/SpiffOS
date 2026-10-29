# ADR — M2 ownership-aware physical page allocation

Status: scoped M2 implementation under the owner's 2026-10-29 authorization
to move memory foundations forward, merge/deploy locally as needed until
2026-10-30 03:00 America/New_York (07:00 UTC). After that instant, no new
merge/deployment without renewed approval. Physical deployment is excluded.
Read with ADR-memory-foundation, ADR-011, ADR-002, ADR-008 and AGENTS.md.

## Decision and scope
Build one pure, caller-owned allocator over complete candidate frames validated
by M1. Allocation returns a physical address through an explicit result/output,
never a dereferenceable pointer or implicit zero sentinel. M2 owns accounting
metadata only. It does not touch candidate-frame contents, change mappings,
provide a heap, enter ring 3 or implement real process/workspace services.

Use one uint16_t ownership slot per candidate frame: zero means free and a
nonzero owner means allocated. This replaces the earlier proposed bitmap for
this checkpoint, avoiding separate bitmap and owner states that could disagree.
A bitmap is smaller for free/busy alone; a free list writes into physical frames;
range lists add splitting/merging complexity. One two-byte owner slot is simple
to teach and costs 2/4096 of managed RAM. Allocation uses bounded first fit.

Owner tokens are internal diagnostic groups, not a user ABI:
1 kernel, 2 system-session, 3..11 workspace 1..9, 12 supervisor; 0 free.
Process subownership/owner registries are deferred to the process milestone.
Group tags enable accounting and wrong-owner free checks, not hardware isolation.

Caller storage must cover EVERY candidate frame. Insufficient metadata capacity,
invalid maps, invalid reserve size or pointers reject initialization before
mutating caller allocator/storage. No silent partial-RAM management.
The boot diagnostic supplies 4,194,304 slots (8MiB BSS), supporting up to 16GiB
of candidate RAM; larger maps report allocator unavailable while preserving
M1 inventory and editor boot. This is a diagnostic bound, not a final OS limit.
Limine already withholds this kernel BSS from its USABLE regions.

## Reserve and operations
The first configured number of candidate-frame ordinals form a reserve.
Input region order is retained; ordinal order need not mean lowest physical RAM.
The boot probe uses 64 reserve frames (256KiB), explicitly a configurable test
budget, not the final supervisor-service resource budget.

Ordinary allocation scans only non-reserve frames. Reserve allocation requires
the supervisor token; it scans only reserve frames. Return explicit OOM without
changing state/output when that eligible pool is exhausted. Being the supervisor
does not silently grant a reserve fallback to an ordinary request.

Free validates initialization, owner, alignment, map membership, allocation and
matching owner before mutation. Reject double frees and wrong-owner frees.
Freed reserve pages return to reserve; all other pages return to ordinary free
space. Maintain per-owner live counts and separate ordinary/reserve free counts:
ordinary_free + reserve_free + sum(owned) == total_frames.
This trusted internal address/owner API does not protect against stale handles
after reuse; generation-based user handles are a future API decision.

No allocation/frame data survives reboot. Map metadata and owner storage are
borrowed exclusively for the allocator lifetime. One boot CPU with interrupts
disabled owns it; locking/concurrent/interrupt use are not implemented.

## Boot integration and validation
Keep M1 inventory and framebuffer behavior. Initialize after validated inventory,
then allocate one ordinary page for each kernel/session/workspace group plus
one reserve supervisor page, prove distinct addresses, ownership rejection and
free/reuse accounting, release all probe pages and emit SPIFFOS_ALLOCATOR_OK.
Emit FRAME_POOL counters before the success marker. Unsupported capacity or
insufficient reserve reports SPIFFOS_ALLOCATOR_UNAVAILABLE and preserves boot.
A self-test inconsistency or serial failure must not claim success.

Host sanitizers cover unsorted regions/holes, frame zero/alignment, complete
capacity checks before mutation, invalid owners, both pools exhausted, wrong
owner and double free, reserve inaccessibility to ordinary requests, reuse,
counters and arithmetic extremes. QEMU at 128/256/512MiB independently checks
M1 counts and initial/free-restored M2 pool counters plus both success markers.
Retain exact editor/workspace frames and safe-launcher cleanup tests.

No tests are claimed by this design record. Operations records supply actual
commands/results, Qwen small jobs/Codex fallback provenance and rollback.
M1 PR18 merged as 81d457b; use an isolated build of that commit for rollback.
Preserve the running owner emulator and its checkout/index/drafts.
