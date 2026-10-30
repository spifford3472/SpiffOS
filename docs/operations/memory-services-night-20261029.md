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

## Checkpoint 2: pure checked page-access policy and host tests

The owner reported that the bounded Qwen attempt for this slice exited 55. Its
log was not available inside this sandbox. Codex therefore inspected the actual
source and clean starting diff rather than assuming Qwen produced or verified a
change, under the memory-specific direct-fallback authorization. No additional
model or agent was delegated.

Codex implemented `page_access_resolve()` and `page_access_zero()` in the new
`src/kernel/page-access.c`, added the file to the strict freestanding build, and
added a focused guarded host test. The pure resolver first checks the complete
M2 ownership/counter state, re-analyzes the borrowed M1 map and requires its
candidate count to match M2. It accepts only aligned, nonzero physical addresses
that identify a complete candidate frame, then requires that ordinal's exact
live non-FREE owner. Physical and direct-map page ranges are checked for
overflow; only 48- and 57-bit x86-64 canonical virtual ranges are supported,
and both the first and last byte must be canonical. Every error preserves the
caller output and allocator metadata.

Numeric addresses remain arithmetic values in policy code. `page_access_zero()`
can act only through a caller-injected synchronous backend after resolution and
passes exactly 4096 bytes. The host backend recognizes one expected numeric
address, translates only that address to guarded ordinary RAM, and verifies
complete zeroing plus untouched guards. Wrong ownership and resolve failures do
not invoke it; backend rejection is explicit and does not release ownership.

The first focused run stopped at the intended upper-bound assertion and exposed
that production code had checked only whether the last byte was representable.
The ADR instead requires the 4096-byte half-open range's exclusive end to be
representable. Production checks were strengthened from `start + 4095` to
`start + 4096`; the existing test was retained rather than weakened. The
corrected implementation passed. A separate case proves rejection when a
page's first byte is canonical but its last byte enters the 48-bit hole.

Actual checks run in this isolated checkout:

- `git diff --check`: passed.
- `make all`: passed strict freestanding compilation and static kernel link,
  including `page-access.c`.
- `ASAN_OPTIONS=detect_leaks=0 make test-page-access test-memory-map
  test-frame-allocator test-keyboard-memory`: passed with AddressSanitizer and
  UndefinedBehaviorSanitizer instrumentation. Reported PASS groups covered M3
  ownership (including supervisor-only reserve ordinals), full-frame
  membership/alignment/arithmetic/48- and 57-bit canonicality/backend guards;
  M1 interval inventory; M2 owners, holes,
  reserve/OOM, rejected frees and counters; keyboard transport/events; logical
  line submission; wrapped input/scroll retention; and editor cursor,
  navigation, insert/overwrite and reflow. LeakSanitizer remained disabled for
  the previously recorded ptrace incompatibility; this is not claimed as a
  LeakSanitizer run.

This checkpoint deliberately did not add Limine HHDM/paging-mode requests, a
kernel pointer-writing backend, allocate-zero-rollback integration, page-table
changes, logging or a boot success marker. No QEMU was started or touched, so
128/256/512 MiB direct-map write/read/zero/reuse evidence and unchanged rendered
editor pixels remain unverified. M1/M2 and editor runtime behavior were not
changed; only their host regressions and the strict link were checked here.

## Checkpoint 3: validated boot direct map and real page probe

The bounded Qwen attempt for this slice exited 53. Its model log was unavailable
inside the Codex sandbox, so no cause or partial success is inferred. Codex
inspected the committed checkpoint-2 source (`f018aca`) and found the pure M3
policy and host tests present, but no HHDM/paging-mode requests, boot adapter,
kernel backend, rollback helper or runtime marker. No model or agent was
delegated under the owner's memory-specific direct-fallback authorization.

Codex added revision-0 Limine HHDM and x86-64 paging-mode requests without
changing the pinned header, Limine binary, base revision or link layout. The
adapter accepts only non-NULL revision-0 HHDM/paging responses and the pinned
header's explicit four- or five-level x86-64 mode. Missing, newer or unknown
responses emit `SPIFFOS_PAGE_ACCESS_UNAVAILABLE` and continue the existing
framebuffer/editor boot. M3 also stays unavailable unless the complete M2 boot
probe restored its pools and emitted its success marker.

`page_access_allocate_zero()` now allocates only from the ordinary pool, uses
the existing exact-owner resolver and injected 4096-byte zero backend, and
publishes the physical address only after success. Resolve or backend failure
releases the page with the same owner and preserves output; a failed release is
reported separately as rollback failure. New host cases prove successful full
zeroing, backend-failure rollback, restored counters/output, policy/allocator
identity, response revision/mode rejection, 48/57-bit width selection and the
unsupported-M2 boot path.

The narrow kernel backend alone converts an authorized direct-map number to a
volatile byte pointer. The boot probe allocates and zeros one kernel page,
verifies every byte is zero, writes and reads `0xa5` across all 4096 bytes,
releases it, requires first-fit reallocation of the same physical page, zeros
and verifies all 4096 bytes again, releases it, then independently checks M2
counters/invariants. Only after all checks and restored pools does it emit
`SPIFFOS_PAGE_ACCESS_OK`; failures emit `SPIFFOS_PAGE_ACCESS_ERROR` and cannot
claim success. No free page, reserve page, framebuffer, page table or CR3 is
written by this probe.

`make test-memory` now depends on the new M3 host test and requires exactly one
page-access success marker with no unavailable/error marker at 128, 256 and
512 MiB. The existing keyboard-ready assertion initially exposed a pre-existing
serial race: ordinary `qemu.py test` terminated as soon as it saw the earlier
`SPIFFOS_BOOT_OK`, and the 256 MiB run was killed before the immediately
following readiness line was captured. The assertion was not removed or
weakened. A serial-only `--wait-keyboard-ready` option now keeps the isolated
guest alive until both complete lines arrive; it does not request QMP or alter
guest behavior.

Actual final checks in this isolated checkout, with no commit made by Codex:

- `ASAN_OPTIONS=detect_leaks=0 make test-page-access test-memory-map
  test-frame-allocator test-keyboard-memory all && git diff --check`: passed.
  ASan/UBSan remained enabled; LeakSanitizer alone stayed disabled for the
  previously recorded ptrace incompatibility. M1 inventory, M2 allocation,
  M3 policy/adapter/rollback, keyboard transport, logical input, scroll
  retention, cursor modes, navigation, insert/overwrite and reflow reported
  PASS; the strict freestanding kernel linked; diff whitespace checks passed.
- Final `ASAN_OPTIONS=detect_leaks=0 make test-memory`: passed its sanitizer
  prerequisites and real QEMU boots. Results were 18,935 candidate frames at
  128 MiB, 51,703 at 256 MiB and 117,239 at 512 MiB, with 32 memory-map entries
  each. Every retained serial log contains one `SPIFFOS_MEMORY_OK`,
  `SPIFFOS_ALLOCATOR_OK`, `SPIFFOS_PAGE_ACCESS_OK`, `SPIFFOS_BOOT_OK` and
  `SPIFFOS_KEYBOARD_READY`, no corresponding unavailable/error marker, and the
  fully restored `FRAME_POOL` counters checked by the script.

An attempted stronger `qemu.py test --keyboard` run did not start a guest:
QEMU was denied while binding its QMP Unix socket with `Operation not permitted`
in this sandbox. Therefore no new QMP-injected keyboard run or guest screenshot
pixel comparison is claimed here. Existing editor pixel/behavior host sanitizer
tests passed, and the successful normal guests reached framebuffer, logo, text,
console and keyboard-ready markers, but unchanged rendered pixels remain a
runtime visual-validation limitation for a less restricted environment. No
owner QEMU, infrastructure, service, firmware, disk, dependency pin, merge,
push or deployment was touched.

## Coordinator recovery — 2026-10-30 02:04 Eastern
The persistent worker stopped at 21:29:35 Eastern after about 17 minutes, not
three hours. Its third checkpoint added a narrowly scoped scripts/qemu.py
readiness wait; the outer commit allowlist omitted that path and raised before
commit/push. No automatic wakeup or merge occurred. M4 heap and M5 cleanup were
not implemented by this run. The first two WIP checkpoints were pushed; the
third remained safely uncommitted in the isolated checkout.

The coordinator reviewed the readiness diff, M3 policy and boot backend, then
ran make test-memory test-keyboard test-workspace test-workspace-scroll and
python3 scripts/test-safe-launcher.py directly on Forge, outside the restricted
model sandbox. Combined exit 0 in 60.36s. ASan/UBSan and leak checks remained
enabled by the normal host invocation. The three real RAM-size probes, exact
editor/workspace/scroll pixel expectations, QMP injection and launcher close/
signal/lock cleanup passed. Evidence: build/night/host-review-gates.log and
build/night/host-review-launcher.log. No model test claim alone was accepted.

The allowlist now explicitly permits only the reviewed qemu.py path alongside
the prior scope. SPIFFOS_RESUME_STEP supports continuing after completed work;
the hard cutoff remains 07:00 UTC, with engineering stopping five minutes
earlier. A restarted queue must not rerun or overwrite completed checkpoints.
No claim that heap or workspace cleanup is ready follows from M3 passing.

## M3 merge/deployment and remaining queue
PR21 head 83720870ed2be4a0e0f63e983787265bbe2c6687 merged as
0beaad849d23e2f6e03570310d7b9dd3cb908d0b under the existing timed approval.
Git diff between tested head and merge was empty, so the already passing
60.36-second full regression result applies to the identical source tree; no
second independent post-merge execution is claimed. Main fast-forwarded cleanly.
The tested ISO was atomically installed for the next QEMU launch, matching SHA256:
ee59cb1725892116b6f88096de53ac2d263be04738d66481cde6715edd850fd6
An initial local main fast-forward failed because that checkout had not fetched
the merge object; fetching then fast-forwarding succeeded before image install.
No running guest was stopped. Heap/cleanup are not part of this deployment.

The remaining queue resumes from zero-based step 3 on feat/memory-heap-night,
using SPIFFOS_WORK_BRANCH and SPIFFOS_RESUME_STEP. It first completes focused M3
error coverage, then heap design/implementation and owner cleanup as time permits.
The resumed service is spiffos-memory-resume-20261030; it stops by 02:55 Eastern,
with no unattended merge/deployment. Original status is preserved in
status-first-attempt.json beside the existing logs. No new three-hour completion
claim is made: the earlier interruption left only the remaining approval window.

## Checkpoint 4: M3 adversarial failure-path host coverage

The owner reported that the small Qwen attempt for this slice exited 53. Its log
was not available inside the sandbox, so Codex did not infer a cause or accept
any unverified model result. The resumed branch worktree was clean at `848a2d5`;
inspection of the actual committed M3 policy, allocator, boot adapter and tests
found the production rollback/output contracts already present. No other model
or agent was delegated.

Codex kept this slice to `tests/page-access-test.c` plus this operations record.
The expanded guarded host matrix now covers null resolve/allocation outputs,
invalid owner rejection, ordinary-pool exhaustion, exact-owner rejection,
resolve failure after allocation but before backend invocation, backend failure
after a partial page write, successful rollback with restored counters/output,
and defensive `PAGE_ACCESS_ROLLBACK_FAILED` reporting when a deliberately
contract-violating backend corrupts the allocated owner slot. The fixture is
restored immediately after that last adversarial branch because the API contract
requires retiring corrupted allocator state in production.

Boundary cases now distinguish mapped-start addition overflow from exclusive-end
overflow and accept exact low/high canonical page boundaries in both 48- and
57-bit modes. Guard bytes are checked after success, partial backend failure,
rollback, null/OOM failures and dirty-page release/reallocation. The reuse case
proves all 4096 dirty bytes are cleared again before the same first-fit physical
page is republished. Test helpers include explicit borrowing, side-effect and
contract comments required by AGENTS.md. No production source change was needed:
the new cases passed against the existing M3 implementation.

Actual checks in this isolated checkout:

- Baseline `ASAN_OPTIONS=detect_leaks=0 make test-page-access`: passed before
  edits with the prior focused matrix.
- After the test expansion, `git diff --check && ASAN_OPTIONS=detect_leaks=0
  make test-page-access`: passed with AddressSanitizer and
  UndefinedBehaviorSanitizer instrumentation. The final marker was `PASS page
  access: ownership, null/OOM failures, rollback, boundaries and backend guards`.
- `ASAN_OPTIONS=detect_leaks=0 make test-memory-map test-frame-allocator
  test-page-access test-keyboard-memory all && git diff --check`: passed. M1
  inventory, M2 ownership/reserve/OOM/rejected-free accounting, expanded M3,
  keyboard events, logical input/submission, scroll retention, cursor modes,
  navigation, insert/overwrite and reflow all reported PASS. The first `all`
  target was already up to date.
- `make -B all`: passed a forced strict freestanding Clang rebuild and static
  link of the complete kernel, including M1, M2, M3 and existing editor code.

LeakSanitizer was disabled only because the prior sandbox/ptrace incompatibility
is already recorded; ASan and UBSan remained active. This checkpoint did not
start, stop or inspect any QEMU, so it adds no new 128/256/512 MiB guest or pixel
evidence and did not disturb the owner's running guest. The already recorded M3
runtime evidence remains applicable to unchanged production code, but is not
reclaimed as a new run. M4 heap and M5 cleanup remain unimplemented and no
commit, merge, push, deployment, dependency, service, firmware or disk change
was performed by this checkpoint.

## Checkpoint 5: bounded M4 heap header and design

The owner reported that the small Qwen attempt for this slice exited 53. Its log
was unavailable inside the sandbox. Codex therefore inspected the clean actual
worktree and found no heap source, header or design change to accept as Qwen
output. Direct Codex fallback was used under the memory-specific authorization;
no model or agent was delegated and no Qwen result is represented as success.

Codex added only `src/kernel/kernel-heap.h` and the detailed M4 contract in
`docs/decisions/ADR-memory-services.md`. The proposed internal heap has a
caller-owned contiguous arena bounded at 4 MiB, 16-byte alignment, a 1-byte
through 1 MiB request range, and at most 1024 caller-owned external metadata
records. Records form a checked address-ordered list. First fit, explicit split
failure on metadata exhaustion, immediate coalescing, exact owner checks and
global/per-owner counters are specified. Invalid pointers are range/alignment
checked and matched by offset; heap code never dereferences them or reads
in-band metadata. The header gives full initialization, allocate, release and
invariant-check contracts, including storage ownership, lifetimes, side effects,
errors and single-CPU/interrupt restrictions required by AGENTS.md.

This is deliberately a header/design checkpoint. There is no heap
implementation, arena/page adapter, boot integration, allocation test, owner
cleanup, M5 lifecycle operation, serial marker or replacement of static editor
storage. The limits are diagnostic internal bounds and establish no user ABI,
quota, final virtual-memory layout or supervisor-reserve size.

Actual checks in this isolated checkout:

- `git diff --check`: passed before the test run.
- Host `cc` and freestanding x86-64 `clang` syntax-only translation units that
  force-include `kernel-heap.h`, using C11 with `-Wall -Wextra -Werror`: passed.
- `ASAN_OPTIONS=detect_leaks=0 make test-memory-map test-frame-allocator
  test-page-access test-keyboard-memory`: passed with AddressSanitizer and
  UndefinedBehaviorSanitizer instrumentation. M1 inventory, M2 ownership,
  reserve/OOM and rejected frees, M3 ownership/rollback/boundaries/backend
  guards, keyboard events, logical input/submission, wrapped console retention,
  cursor modes, navigation, insert/overwrite and reflow all reported PASS.
- `make -B all`: passed a forced strict freestanding rebuild and static kernel
  link of the unchanged M1/M2/M3 and editor runtime sources.

LeakSanitizer alone was disabled because the earlier sandbox/ptrace failure is
already documented; no LeakSanitizer pass is claimed. The heap header is not
included by runtime code, so these are contract syntax and existing-regression
checks, not allocator behavior tests. No QEMU was started, stopped or inspected;
there is no new guest, pixel or runtime-heap evidence, and the owner's QEMU was
not touched. No commit, merge, push, deployment, dependency pin, runner control,
service, infrastructure, firmware or disk was changed.
