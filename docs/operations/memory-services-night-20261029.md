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

## Checkpoint 6: bounded M4 initialization and allocation

The owner reported that the small Qwen attempt for this slice exited 55. Its log
was not available inside the sandbox, so Codex verified the actual clean source
at `e64cd24` and found the M4 header/design but no heap implementation or tests.
Direct Codex fallback was used under the memory-specific authorization. No model
or agent was delegated, and no Qwen source or verification result is claimed.

Codex added `src/kernel/kernel-heap.c` for the allocation-only M4 slice. Heap
initialization validates the 16-byte-aligned arena bound, record-count
multiplication, representable half-open address ranges, record alignment, all
descriptor/arena/record-array disjointness and the required unused zeroed
descriptor before changing caller storage. Success creates one address-ordered
free block and clears inactive external records. `kernel_heap_check()` validates
bounded and reciprocal links before dereference, active-record reachability,
strict gap-free coverage, aligned nonzero ranges, owner/state agreement, no
adjacent free records and independently recomputed global/per-owner counters.

Allocation uses checked 16-byte rounding and bounded first fit. Exact fits need
no additional record; a larger first fit requires a reusable inactive record or
returns `KERNEL_HEAP_METADATA_EXHAUSTED` without consuming the block. All
fallible validation, split-capacity discovery and counter checks complete before
payload writes. The complete rounded block is then zeroed, metadata/counters are
updated and the pointer is published last. Null, bad-owner, zero/oversized,
ordinary OOM, metadata exhaustion, aliased-output and corrupt-descriptor failures
preserve output, descriptor, records and arena bytes. This owner checkpoint's
explicit zero-before-publish requirement supersedes the design header's earlier
uninitialized-return sentence; the preserved history and refinement are recorded
in `ADR-memory-services.md` and the public function contract.

The guarded host test obtains one kernel page through the real M2/M3
`page_access_allocate_zero()` path, using a narrow injected mapping backend. It
proves the ordinary pool decreases while the one-page supervisor reserve remains
untouched, initializes M4 over that validated owned page, dirties the kernel-only
arena to model reuse, and verifies complete rounded zeroing, aligned split
pointers, per-owner/global counters and untouched guard bytes. Additional cases
cover checked initialization failures, exact fit with one metadata record,
metadata exhaustion, OOM, zero/`SIZE_MAX`/over-limit requests, owner rejection,
output alias rejection and corrupt-list failure atomicity. `test-memory` now
depends on `test-kernel-heap`, and the heap object participates in the strict
freestanding kernel link.

Actual checks run in this isolated checkout:

- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-kernel-heap`:
  passed with AddressSanitizer and UndefinedBehaviorSanitizer; the marker was
  `PASS kernel heap: owned ordinary backing, init, zero, split and atomic
  failures`.
- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-memory-map
  test-frame-allocator test-page-access test-kernel-heap test-keyboard-memory &&
  make -B all`: all host tests passed, then the initial forced freestanding link
  failed because Clang lowered an aggregate record/descriptor clear to an
  unavailable hosted `memset`. Tests were not weakened. Production initialization
  was changed to explicit field stores and to reuse the already-proven zeroed
  descriptor state.
- `ASAN_OPTIONS=detect_leaks=0 make test-kernel-heap && make -B all && git diff
  --check` after that correction: passed. The strict Clang compile and static
  link include `kernel-heap.c` without a C runtime.

LeakSanitizer alone was disabled because the previously recorded ptrace
incompatibility applies in this sandbox; ASan and UBSan remained enabled. The
full `make test-memory` target was not run because it starts QEMU; this checkpoint
did not start, stop or inspect any QEMU and therefore did not disturb the owner's
guest. There is no new 128/256/512 MiB guest, editor-pixel or runtime heap-marker
evidence. M4 still relies on its caller to supply a virtually contiguous arena
assembled from M3-validated, kernel-owned ordinary pages; it does not infer
contiguity or consume frames itself. `kernel_heap_release()`, coalescing, heap
teardown/backing-frame return, boot integration and M5 owner cleanup remain
unfinished. No commit, merge, push, deployment, dependency pin, runner control,
service, infrastructure, firmware or disk was changed.

## Checkpoint 7: checked M4 release, coalescing and reuse

The owner reported that the small Qwen attempt for this slice exited 55. Its log
was not available inside the sandbox, so Codex inspected the actual clean source
instead of inferring a cause or accepting any Qwen implementation or test claim.
The source contained the completed checkpoint-6 initialization/allocation path
but no `kernel_heap_release()` implementation or release tests. Direct Codex
fallback was used under the memory-specific authorization; no model or agent was
delegated.

Codex implemented only the existing M4 release contract in
`src/kernel/kernel-heap.c`. Release first validates the owner and complete heap
graph/accounting state, converts the supplied pointer to an integer solely for
checked arena-range/alignment classification, and searches external active
records for an exact block-start offset. It never dereferences caller-supplied
allocation memory as metadata. Null, foreign, unaligned interior, aligned
interior, stale coalesced, wrong-owner, double-free and corrupt-heap paths return
their documented errors before mutation.

A valid release subtracts the live allocation and owner counters exactly once,
leaves payload bytes unchanged, and immediately merges a free predecessor and/or
successor. Coalesced-away records are reset through explicit field stores and
returned to inactive metadata capacity; reciprocal links and the first-block
identity remain intact. No fallible operation occurs after mutation begins.
The existing independent `kernel_heap_check()` accepts the resulting graph and
recomputed global/per-owner accounting.

The guarded sanitizer test now creates separated 32-byte and 64-byte holes with
96 total free bytes and proves an 80-byte request initially returns OOM without
changes. Releasing the 16-byte allocation between them exercises two-sided
coalescing into one 112-byte range. First fit then reuses the earliest old hole,
clears all 80 republished bytes, reuses retired metadata for the remainder, and
preserves both guard regions. Separate atomic-rejection cases cover null,
foreign, unaligned and aligned interior pointers, invalid and wrong owners,
current-boundary double free, stale boundary after coalescing and corrupt
metadata. They compare the complete descriptor, record array and arena bytes
before and after every rejected operation. A successful free is also checked to
retain dirty payload bytes while preserving global/per-owner counters.

Actual checks run in this isolated checkout:

- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-kernel-heap`:
  passed with AddressSanitizer and UndefinedBehaviorSanitizer. The final marker
  was `PASS kernel heap: checked free, coalescing, reuse and atomic failures`.
- `ASAN_OPTIONS=detect_leaks=0 make test-memory-map test-frame-allocator
  test-page-access test-kernel-heap test-keyboard-memory && make -B all && git
  diff --check`: passed. M1 inventory, M2 ownership/reserve/OOM/rejected frees,
  M3 ownership/rollback/boundaries/backend guards, the expanded M4 test,
  keyboard transport, logical input/submission, scroll retention, cursor modes,
  navigation, insert/overwrite and reflow all reported PASS. The forced strict
  freestanding Clang compile and static kernel link included the release code.

LeakSanitizer alone remained disabled because the previously recorded sandbox
ptrace incompatibility still applies; no LeakSanitizer pass is claimed. This
checkpoint did not start, stop or inspect QEMU, so it did not disturb the
owner's guest and adds no 128/256/512 MiB guest, serial heap marker or editor
pixel evidence. The heap remains unintegrated with the boot path and still
depends on caller-supplied virtually contiguous M3-validated kernel-owned
ordinary backing. Heap teardown/backing-frame return and M5 owner-scoped cleanup
remain unfinished. No commit, merge, push, deployment, dependency pin, runner
control, service, infrastructure, firmware or disk was changed.

## Checkpoint 8: actual M4 heap boot smoke probe

The owner reported that the small Qwen attempt for this slice exited 53. Its
log was unavailable inside this sandbox, so Codex did not infer a cause or
accept any Qwen source/test result. Inspection of the clean actual branch found
the completed host-tested M4 allocator/release implementation but no boot heap
probe, heap serial marker or guest screenshot validation. Direct Codex fallback
was used under the memory-specific authorization; no model or agent was
delegated.

Codex added one boot-only `memory_boot_heap_probe()` after the existing M3 page
probe. It obtains one ordinary kernel frame exclusively through
`page_access_allocate_zero()`, resolves it through the validated revision-0
HHDM/paging policy, and uses its complete 4096-byte mapping as the bounded heap
arena. Eight external records and the single-boot descriptor remain in kernel
BSS, outside the arena and supervisor reserve. The probe requires 16-byte
alignment, writes and reads distinct byte patterns through kernel/session/
workspace allocations, checks per-owner counters and rejects a wrong-owner
free. A 4096-byte request after live allocations must return OOM without
changing its output or dirty live payload. A subsequent valid free/allocation
must recover, reuse the same first-fit address, publish all 32 rounded bytes
zeroed, and permit another write/read check.

Success releases every allocation, proves one completely coalesced free page
and zero owner counters, explicitly retires the descriptor, releases the exact
kernel-owned backing frame and verifies that ordinary and reserve M2 counters
match their pre-probe values. Only then is `SPIFFOS_HEAP_OK` emitted. Missing
M3 policy emits `SPIFFOS_HEAP_UNAVAILABLE`; any self-test or restoration failure
emits `SPIFFOS_HEAP_ERROR` and cannot emit the success marker. The existing
unavailable host-adapter test was extended, and `test-memory.py` now requires
the heap marker at 128/256/512 MiB, retains one editor PPM per size and runs the
independent full-frame pixel checker.

The first forced kernel link after integration failed honestly: Clang lowered a
local aggregate descriptor clear to unavailable hosted `memset`. No linker or
test was weakened. The descriptor was moved to zero-initialized bounded BSS and
is retired with explicit field stores; the next forced freestanding link passed.

Actual checks during this checkpoint in the isolated checkout:

- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-memory-map
  test-frame-allocator test-page-access test-kernel-heap test-keyboard-memory
  test-workspace-memory && make -B all && make iso`: passed. AddressSanitizer
  and UndefinedBehaviorSanitizer remained enabled for host tests; M1 inventory,
  M2 ownership/reserve/OOM/rejected frees, M3 rollback/boundaries, M4 checked
  free/coalescing/reuse, keyboard/editor/reflow and workspace/logo reset tests
  all reported PASS. The strict freestanding kernel compiled and linked without
  a hosted runtime, and the ISO rebuilt.
- The first `ASAN_OPTIONS=detect_leaks=0 make test-memory` attempt passed all
  host prerequisites and built the ISO, but its 128 MiB guest never started:
  this sandbox denied QEMU binding the private QMP Unix socket with `Operation
  not permitted`. The target correctly failed; no guest result is claimed from
  that attempt, and the screenshot requirement was not removed.
- Preliminary serial-only isolated `scripts/qemu.py` guests passed at all three
  sizes without QMP. After the final descriptor-lifetime tightening and complete
  rebuild, the retained HMP-capture guests below repeated the same serial checks
  on the final source rather than relying on those preliminary runs.
- To retain the required visual evidence despite the sandbox's Unix-socket
  denial, three additional isolated guests used QEMU's inherited standard HMP
  monitor only for `screendump`; no repository launcher/runner control was
  edited. `scripts/check-framebuffer.py --powerup --console --workspace` passed
  every pixel of all three 1280x800 captures against the independent logo/font/
  editor-cursor reference. Their final serial results were 18,926 candidate
  frames at 128 MiB, 51,694 at 256 MiB and 117,230 at 512 MiB, with 32 map
  entries each. Independent verification required exactly one each of
  `SPIFFOS_MEMORY_OK`, `SPIFFOS_ALLOCATOR_OK`, `SPIFFOS_PAGE_ACCESS_OK` and
  `SPIFFOS_HEAP_OK`, framebuffer/boot/keyboard readiness, no corresponding
  unavailable/error marker, restored frame-pool counters and increasing
  recomputed capacity. The retained files are
  `build/memory-128-editor.ppm`, `build/memory-256-editor.ppm` and
  `build/memory-512-editor.ppm`; each SHA-256 is
  `d22bd34e7998e134c9ec8824ce3e68cddca003234b1a3e87c9fc79a3eeb46863`.
  Their matching per-size serial logs are `build/memory-*-serial.log`.

LeakSanitizer was disabled only for the previously documented sandbox/ptrace
incompatibility; ASan and UBSan passed. The QMP-backed combined `make
test-memory` target is not claimed as a pass in this sandbox, although its exact
serial and pixel assertions were exercised through the separated successful
guest runs above. This is still a one-page boot diagnostic with static external
metadata, not a persistent general kernel heap, multi-page contiguous arena,
user ABI, quota, VM layout or supervisor-reserve consumer. M5 owner-scoped
cleanup remains unfinished. No owner QEMU was inspected or stopped; each test
ended only its own child. No commit, merge, push, deployment, dependency pin,
runner control, service, infrastructure, firmware or disk was changed.

## Checkpoint 9: M5 owner-scoped workspace cleanup and session lifetime

The owner reported that the small Qwen attempt for this slice exited 53. Its
log was not available inside the sandbox, so Codex inspected the clean actual
source rather than inferring a cause or accepting any Qwen implementation or
test result. The tree contained the completed M4 heap and guest probe but no M5
cleanup API, cleanup tests or serial marker. Direct Codex fallback was used
under the memory-specific authorization; no model or agent was delegated.

Codex added two deliberately separate internal lifecycle entry points.
`kernel_heap_release_workspace()` accepts only workspace 1..9 owner tokens and
rejects FREE, kernel and session tokens. `kernel_heap_release_session()` has no
owner parameter and can release only session allocations. Both validate the
complete external-metadata graph and independently recomputed counters before
mutation, retain freed payload bytes, release every block in the selected
lifetime and coalesce all newly adjacent free ranges. Empty-owner cleanup is
idempotent. Neither operation calls M2, releases the M3 arena backing frame nor
has a path to the supervisor reserve. Corrupt metadata and invalid workspace
tokens are rejected without changing descriptor, records or arena bytes.

The guarded host test creates live kernel, session, workspace 1 and workspace 2
blocks over a real M2/M3-authorized ordinary kernel page. It resets two
non-adjacent workspace-1 allocations, then proves workspace-2, kernel and
session counters and dirty payloads remain unchanged. A byte-for-byte snapshot
of the M2 descriptor and all ownership slots proves the backing frame and the
one-page supervisor reserve are unchanged. It then invokes the separate session
teardown and proves kernel/workspace-2 remain live. Invalid-owner, corrupt-heap,
empty-owner, coalescing and final frame-restoration cases are also checked.

The real boot probe performs the same lifetime separation inside the validated
one-page direct-map heap. It retains kernel, session and workspace 2 across a
workspace-1 reset; retains kernel/workspace 2 across session teardown; checks
ordinary and reserve frame counters throughout; then releases the remaining
blocks/backing and restores M2. Only after all those checks does it emit
`SPIFFOS_OWNER_CLEANUP_OK`, followed by the existing `SPIFFOS_HEAP_OK`.
`scripts/test-memory.py` now requires exactly one cleanup marker.

Actual checks run in this isolated checkout:

- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-kernel-heap`:
  passed with AddressSanitizer and UndefinedBehaviorSanitizer. The marker was
  `PASS kernel heap: checked free, owner cleanup, coalescing and isolation`.
- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-memory-map
  test-frame-allocator test-page-access test-kernel-heap test-keyboard-memory
  test-workspace-memory && make -B all && git diff --check`: passed. M1
  inventory, M2 ownership/reserve/OOM/rejected frees, M3 access/rollback, M4/M5
  heap behavior, keyboard/editor/reflow and workspace/logo-reset host tests all
  passed. The forced freestanding Clang compile and static link included M5.
- `ASAN_OPTIONS=detect_leaks=0 make test-memory` passed all host prerequisites
  and rebuilt the ISO, but the QMP-backed 128 MiB child produced no serial
  output and the target failed with `serial marker absent`. No guest or pixel
  result is claimed from that failed attempt, and the screenshot assertion was
  not removed or weakened.
- Separate real serial-only child guests passed at 128, 256 and 512 MiB with
  18,923, 51,691 and 117,227 candidate frames respectively. Each emitted
  exactly one `SPIFFOS_OWNER_CLEANUP_OK`, retained the existing M1-M4,
  framebuffer, console, boot and keyboard-ready markers, and showed the restored
  `FRAME_POOL` counters with all 64 reserve frames free. Each command used
  `scripts/qemu.py test --wait-keyboard-ready` without QMP and ended only its
  own child.
- A final-source 128 MiB child used the established inherited HMP monitor only
  for `screendump`, then exited through its own `quit` command.
  `scripts/check-framebuffer.py --powerup --console --workspace` passed every
  pixel of the 1280x800 capture. The capture is
  `build/m5-owner-cleanup-editor.ppm`, SHA-256
  `d22bd34e7998e134c9ec8824ce3e68cddca003234b1a3e87c9fc79a3eeb46863`.
  Its serial log contains one cleanup marker, one heap marker and no
  `SPIFFOS_*ERROR`; the log SHA-256 is
  `ed80379c3c5cce731860666dd9e6e2c06281dda7745cdc17177c8359c2724b11`.

The capture checksum above is recorded from the actual command output; this
checkpoint does not claim the combined QMP-backed `make test-memory` target
passed. LeakSanitizer alone remained disabled for the already documented
sandbox/ptrace incompatibility; ASan and UBSan remained enabled. This is trusted
internal heap ownership bookkeeping and a boot diagnostic, not a real workspace
reset UI, process manager, ring-3 isolation boundary, persistent session service
or final teardown policy. No owner QEMU was inspected or stopped. No commit,
merge, push, deployment, dependency pin, AGENTS/runner control, service,
infrastructure, firmware or disk was changed.

## Checkpoint 10: independent deterministic M4-M5 accounting model

The owner reported that the small Qwen attempt for this slice exited 55. Its
log was not available inside the sandbox, so Codex inspected the actual clean
source at `0c9a2eb` and did not infer that Qwen changed or verified anything.
The existing source already contained checked M4 allocation/release and M5
workspace/session cleanup plus focused tests. Direct Codex fallback was used
under the memory-specific authorization. No model or agent was delegated.

Codex added a separate host test, `tests/kernel-heap-model-test.c`, without
changing the heap API or production allocator. Its oracle represents the
4096-byte arena as 256 independent 16-byte units tagged by allocation identity,
not by production record index. This distinction lets it predict exact block
boundaries even for adjacent allocations with the same owner. After every one
of 4,000 deterministic operations it independently recomputes global and
per-owner byte/allocation counts, walks the production graph in address order
and compares each allocated/free run, checks every live payload pattern and
both fixed guard regions, then additionally requires the production invariant
checker to agree.

The sequence mixes all eleven live owner groups, first-fit allocations, exact
releases, repeated idempotent workspace cleanup and separate session cleanup.
Requests larger than the arena and fragmented-space failures exercise OOM;
each expected failure snapshots and compares the complete descriptor, all 256
external records, output pointer, arena and guards. Zero, over-limit and
`SIZE_MAX` requests cover size/rounding overflow rejection with the same atomic
comparison. Every successful allocation is checked byte-for-byte zero before
being dirtied, so later reuse proves the entire rounded block is cleared. Final
owner cleanup plus individual kernel release must reduce both model and
production to one free 4096-byte record, proving full coalescence. Fixed lower
bounds ensure the selected sequence actually completes more than 500 successful
allocations, 50 OOM results, 100 workspace cleanups and 20 session cleanups.
The existing `test-kernel-heap` target now runs both the focused test and this
model-based test under the same sanitizer flags.

No concrete production failure was found, so no allocator implementation or
contract was changed. Actual checks run in this isolated checkout:

- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-kernel-heap`:
  passed with AddressSanitizer and UndefinedBehaviorSanitizer. Both the existing
  focused marker and `PASS kernel heap model: deterministic owners, OOM,
  cleanup and reuse` were reported.
- `ASAN_OPTIONS=detect_leaks=0 make test-memory-map test-frame-allocator
  test-page-access test-kernel-heap test-keyboard-memory
  test-workspace-memory && make -B all && git diff --check`: passed. M1
  inventory, M2 owner/reserve accounting, M3 access/rollback, both M4-M5 heap
  suites, keyboard/logical-line/editor behavior and workspace/logo reset all
  reported PASS. The forced strict freestanding Clang build and static kernel
  link also passed.

LeakSanitizer alone remained disabled because the previously recorded
sandbox/ptrace incompatibility applies; ASan and UBSan remained enabled. This
test uses a deterministic fixed sequence rather than exhaustive state-space or
concurrent/interrupt testing. No QEMU was started, stopped or inspected, so no
new guest, serial-marker or framebuffer evidence is claimed and the owner's
running guest was not disturbed. Existing M1-M5 runtime evidence applies only
to its previously tested source; this checkpoint changes host-test wiring only.
No commit, merge, push, deployment, dependency pin, AGENTS/runner control,
service, infrastructure, firmware or disk was changed.

## Checkpoint 11: fresh complete memory-diff correctness review

The owner reported that the small Qwen attempt for this slice exited 55. Its
log was not available inside this sandbox. Codex therefore reviewed the actual
clean feature source at `3813ce4` and the complete M3-M5 diff from the recorded
night-job base, without assuming Qwen changed or verified anything. No model or
agent was delegated. The review covered mapping authorization, physical range
bounds, metadata lifetime, owner accounting, reserve isolation, rollback,
invalid frees, zero-before-publish and unsupported Limine response paths.

Three actionable contract gaps were corrected. First, a caller could supply an
M3 result pointer that overlapped the policy, allocator descriptor, borrowed M1
region array or used M2 owner slots. A successful publication could therefore
overwrite the metadata that authorized it. `page_access_resolve()` and
`page_access_allocate_zero()` now reject every such output range before writing
or allocating. Second, allocate-and-zero previously let `frame_allocate()` be
the first consistency check and relied on the later resolver to notice broader
M2 counter/slot corruption after acquisition. It now runs the complete
`frame_allocator_check()` before mutation. Adversarial host cases prove exact
policy/allocator output aliases and corrupt accounting preserve all metadata,
output bytes and backend call count.

Third, a boot-heap self-test failure after M4 initialization released the
kernel-owned backing frame but left the static descriptor initialized and still
pointing at that page. The boot path halted immediately, but the metadata
lifetime nevertheless violated the stated ownership contract. Every heap-probe
failure now retires the descriptor before attempting to return the frame to M2;
the successful path already used that ordering. Retirement only clears the
static descriptor and never dereferences the arena or stale payload pointers.

The remaining reviewed paths matched their documented bounds and ownership
rules. M1 rejects overflowing/overlapping USABLE ranges and admits only complete
aligned nonzero frames. M3 checks the physical exclusive end, mapped exclusive
end and first/last canonical bytes, then requires exact live ownership. Ordinary
allocate-and-zero never requests reserve, and M4/M5 cannot discover or release
the M2 reserve. Backend failure rolls back with the exact owner and reports a
distinct rollback failure if corrupted metadata prevents release. Heap release
classifies foreign/interior/stale, double and wrong-owner pointers before
mutation. Both page publication and rounded heap-block publication clear the
complete range first. Missing/newer HHDM or paging responses and unknown paging
modes retain the non-fatal unavailable path; there is still no identity-map
fallback.

Actual checks in this isolated checkout:

- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-page-access
  test-kernel-heap test-memory-map && make -B all`: passed. The strengthened M3
  failure matrix, both M4/M5 suites, M1 adapter checks and a forced strict
  freestanding compile/static link all passed.
- `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-memory-map
  test-frame-allocator test-page-access test-kernel-heap test-keyboard-memory
  test-workspace-memory && git diff --check`: passed. ASan/UBSan reported the
  M1 inventory, M2 owners/reserve/OOM/rejected frees, M3 alias/accounting/
  rollback/boundary cases, focused and deterministic M4/M5 accounting, keyboard,
  logical-line/editor reflow and workspace/logo-reset groups as PASS.
- After adding the final borrowed-region output-alias case,
  `git diff --check && ASAN_OPTIONS=detect_leaks=0 make test-page-access && git
  diff --check` passed again with ASan/UBSan.
- `make iso && python3 scripts/qemu.py test --wait-keyboard-ready --memory-mib
  128 --timeout 30`: passed in one launcher-locked child guest and ended only
  that child. The retained `build/checkpoint-review-128.log` contains 32 map
  entries, 18,922 candidate frames, restored `FRAME_POOL:18922,18858,64,64,0`,
  exactly one each of `SPIFFOS_MEMORY_OK`, `SPIFFOS_ALLOCATOR_OK`,
  `SPIFFOS_PAGE_ACCESS_OK`, `SPIFFOS_OWNER_CLEANUP_OK`, `SPIFFOS_HEAP_OK`,
  `SPIFFOS_BOOT_OK` and `SPIFFOS_KEYBOARD_READY`, with no memory-service error
  or unavailable marker. Calling the existing `verify_serial()` directly over
  the retained serial log independently recomputed 77,504,512 candidate bytes
  and the same pool counters.

LeakSanitizer alone was disabled because of the already recorded sandbox/ptrace
incompatibility; ASan and UBSan remained enabled. This review did not repeat the
256/512 MiB guests because the fixes affect pre-mutation metadata validation and
failure-path retirement, not RAM-size-dependent success behavior, and the owner
explicitly requested evidence-driven tests rather than repetition. No fresh
QMP keyboard injection or framebuffer screenshot/pixel comparison is claimed;
the full host editor/workspace regressions passed, and the 128 MiB guest reached
framebuffer, console, boot and keyboard-ready markers. The descriptor-retirement
failure ordering was verified by source review and the freestanding build, not
by fault-injecting a real guest failure. Stale-handle generations, SMP/interrupt
synchronization, persistent multi-page heap service, ring-3 isolation and final
reserve sizing remain outside M3-M5. No owner QEMU, commit, merge, push,
deployment, dependency pin, AGENTS/runner control, service, infrastructure,
firmware or disk was changed.

## Coordinator completion — 2026-10-30, renewed daytime authority
The owner renewed reviewed local merge/deployment approval through 14:00
America/New_York today (18:00 UTC). The resumed worker had completed page error
coverage, heap contracts, allocation, coalescing free, guest integration, owner
cleanup, independent model tests and invariant review. It stopped at 02:55
Eastern during packaging (worker exit 124); no complete packaging pass or
automatic merge was claimed. Qwen slices reached their bounded turn/tool/time
limits and Codex completed the focused fallback work, as retained per-step logs
show. The coordinator completed review, teaching/status documentation and
release packaging under the renewed authority. No new runtime algorithm was
written by the coordinator in this completion pass; only a stale header comment
was corrected alongside documentation.

The coordinator read AGENTS, accepted/open, system overview, memory ADRs, the
complete heap implementation/contracts, M3 changes, boot lifecycle probe and
focused/model test structure. Source review confirmed range checks precede
mutations, exact-owner frees, external metadata, zero-before-publication,
workspace/session lifetime separation, coalescing and descriptor retirement
before backing ownership ends.

Fresh unrestricted Forge validation: make test-memory test-keyboard
test-workspace test-workspace-scroll followed by python3 scripts/test-safe-launcher.py
completed with combined exit 0 in 61.19 seconds. Normal host ASan/UBSan and leak
checks were enabled; no ASAN_OPTIONS workaround was needed. Evidence:
build/night/day-validation.log and day-launcher.log. The full matrix included
M1-M5 host suites, deterministic model/guard checks, real 128/256/512 MiB guests,
independent serial counters, all heap/cleanup success markers, exact initial
workspace pixels at every RAM size, real keyboard injection and five editing
captures, forced scrolling, launcher locking and close/signal cleanup.

| RAM MiB | Candidate frames | Ordinary free restored | Reserve free restored |
|---|---:|---:|---:|
| 128 | 18922 | 18858 | 64 |
| 256 | 51690 | 51626 | 64 |
| 512 | 117226 | 117162 | 64 |

All three initial workspace captures share SHA256
d22bd34e7998e134c9ec8824ce3e68cddca003234b1a3e87c9fc79a3eeb46863
and independently passed the existing full-pixel expectations.
git diff --check passed after final documentation/comment changes.

This checkpoint is a usable bounded allocator library and reversible one-page
boot proof, not a persistent multi-page runtime heap or real workspace manager.
The next checkpoint must establish retained backing/lifetime and a small memory
service before moving real session/workspace state off static storage. SMP,
interrupt allocation, quotas, stale-handle generations and ring-3 protection
remain open. Rollback: isolated build of M3 merge 0beaad8, preserving current
checkouts/history. Physical disks, USB, firmware, network and infrastructure
remain untouched. Manual GUI acceptance can occur when the owner gets home.

## PR22 merge and local deployment — 2026-10-30
Reviewed head 923508f06d4be17cdbc66b4aed4687e11649b007 merged as
401d92c054d0aa521c6242ae983ef410f469d677 under renewed owner authority
through 14:00 Eastern. Git diff from head to merge was empty. The already
verified runtime source is unchanged by the final teaching/status documentation
and one header-comment correction; no extra post-merge test execution is
claimed. Full unrestricted coordinator gate evidence remains the 61.19-second
passing run recorded above.

The clean /home/geoff/projects/spiffos-qemu-main checkout fast-forwarded to
401d92c. The tested feature-build ISO was copied to a .next file and atomically
renamed into its build/spiffos.iso. Both validation/deployed SHA256 values match:
47dc4a57d816355fbb507b82a71d8162d75d6d6c06841a95572ce3742fd62204
No guest was terminated; no owner guest process was observed running at this
check. The next launch uses the updated image; manual GUI testing remains for
the owner when home. No physical media, firmware or infrastructure was changed.

All M3-M5 runtime work is now merged. Persistent retained backing and real
session/workspace allocation are the next checkpoint; the failed/stopped night
services have not been restarted or repurposed for that additional work.
