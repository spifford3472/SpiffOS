# Memory services night job — 2026-10-29

Status: prepared, not yet a claim of runtime implementation or passed tests.
Scope: ADR-memory-services M3 checked access/zeroing, M4 heap, M5 owner cleanup.
Isolated clone: /home/geoff/projects/spiffos-memory-night-20261029
Branch: feat/memory-services-night; base d41f77f. Existing owner checkouts and
running QEMU remain untouched. The committed runner is copied outside the
model workspace so worker edits cannot change the running controller.

## Execution
Transient user service spiffos-memory-night-20261029 has a three-hour engineering
budget and a hard runtime limit of 3h10m, capped before the 03:00 Eastern merge
approval deadline. It takes the existing single-worker lock. Each Qwen slice is
one file/function with 180s model budget and 200s outer timeout; each focused
Codex verification/fallback has at most 1100s. Local Qwen timeout history is in
prior memory records; each new attempt/result is independently retained here.
The 12-step queue follows dependencies, with no parallel coding workers.

Status and logs: /home/geoff/.local/share/ki5vnb-coding/runs/spiffos-memory-night-20261029/
status.json is atomically updated; progress.log and per-model logs retain evidence.
The runner may commit WIP feature progress and push only its feature branch.
A pushed commit is not a test pass. Final ASan/UBSan/QEMU/editor/launcher gates
and actual milestone markers determine readiness for review, not auto-merge.
No autonomous main push, merge, deployment, physical-media, network or service
configuration is permitted. The owner's timed merge authorization remains valid
for a subsequent review of concrete passing checkpoint results.

## Recovery and limits
Stop only this job with systemctl --user stop spiffos-memory-night-20261029.
Partial changes remain in the isolated clone and feature commits; inspect status
and logs before resuming. Do not restart the entire queue blindly or reset useful
work. Codex CLI readiness was verified on this branch before launch; the first
stdin-open smoke invocation was terminated after diagnosis and a DEVNULL-stdin
retry returned READY. No kernel tests were claimed by that readiness smoke.
No new shell needed from the owner; job progress can be checked through the
status file and systemctl --user status spiffos-memory-night-20261029.

## Checkpoint 1: M3 page contract

The bounded Qwen attempt for this slice exited 53. Its model log was not
available inside the Codex sandbox, so no diagnosis is inferred from that code.
The worktree was clean afterward: inspection found no Qwen source or
documentation change to retain. Under the owner's memory-specific Codex
fallback authorization, Codex read AGENTS.md, accepted/open, system overview,
ADR-011, ADR-memory-foundation, ADR-frame-allocator, the current M1/M2 source,
the pinned Limine declarations/provenance and local boot records.

Codex finalized only the M3 direct-map/access contract and added
`src/kernel/page-access.h`; there is no page-access implementation, boot request,
page-table manager or change to M1/M2/editor behavior in this checkpoint. The
contract requires revision-0 HHDM and paging-mode responses from the exact
pinned protocol/binary pair, accepts only reported x86-64 4- or 5-level modes,
and makes missing/unsupported responses non-fatal. Pure policy must validate a
complete USABLE candidate frame, its exact live M2 owner, range arithmetic and
canonical direct-map addresses. Only an injected backend may touch the page.

Local evidence checked here: `include/limine.h` SHA-256 matched the recorded
protocol provenance; it declares the HHDM offset and x86-64 paging-mode values.
`.local-dev/limine` was at the recorded v10.8.5 binary commit, while the prior
boot record contains actual base-revision-5 QEMU acceptance. This proves the
selected request ABI/pair, not that a particular boot supplied a usable HHDM;
that runtime proof remains required when the adapter is implemented.

Actual checks for this documentation/header-only slice:

- `git diff --check`: passed.
- Host `cc` and freestanding x86-64 `clang` syntax-only translation units that
  force-include `page-access.h`, with `-std=c11 -Wall -Wextra -Werror`: passed.
- `make all`: passed the strict freestanding compile and link.
- The first plain `make test-frame-allocator` was blocked before test output by
  LeakSanitizer's fatal `does not work under ptrace` environment error. The test
  uses no heap allocation. `ASAN_OPTIONS=detect_leaks=0 make
  test-frame-allocator test-keyboard-memory` then passed with AddressSanitizer
  and UndefinedBehaviorSanitizer instrumentation retained: allocator owners,
  holes, reserve/OOM, rejected frees and counters; keyboard transport/events;
  logical input; exact submission; scroll retention; cursor modes; navigation;
  insert/overwrite and reflow all reported PASS. This workaround is not claimed
  as a LeakSanitizer run.

M3 implementation and sanitizer behavior tests, direct-map QEMU
write/read/zero/reuse evidence and `SPIFFOS_PAGE_ACCESS_OK` remain unfinished.
