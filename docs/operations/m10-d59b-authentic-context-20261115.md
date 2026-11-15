# D5.9-B authentic execution and real boot-frame transaction checkpoint

Date: 2026-11-15. Isolated development only; **D5.9-B and M11 are not complete**.
Branch: `feat/m10-d59b-authentic-context`, parent PR #90 at
`8f77e47d7c5282d60d7890a7636b9a76e3972f60` (CI run #107 SUCCESS).
The parent #85–#89 exact heads were independently checked: all successful.

## Contracts and implementation

Read root AGENTS.md, accepted/open registers, system-overview, developer guide,
D5.7 safety source/ADR, D5.9 audit and allocator exclusion ADR, and the docs-first
`ADR-m10-d59b-authentic-context.md`. Documentation phase committed first as
`8b34b95` on Forge. The connected GitHub API published its byte-identical
source-tree twin as `a2a8ffb97bf1f26b6ce58af2fd8bd6bb58014154` after shell
push reported no HTTPS credentials. No credential installation or host changes.
Existing D5.7 is the only guard owner/depth/cookie/fault authority;
M10-A remains sole task-state authority. No duplicate scheduler or lock.

- `src/kernel/m10-d59b-context.[ch]`: genuine PUSHFQ/CLI/IF sampling, boot CPU
  CPUID initial APIC identity, actual IRQ nesting, live RSP against D5.2b stack
  binding and authoritative RUNNING generation. Strict LIFO through D5.7;
  invalid ownership, nesting or context permanently faults and keeps IF off.
  Nested entry inherits IF=0; successful final exit alone restores original IF.
  No boot/no-task bypass. Bootstrap and final off-stack cleanup retain their
  existing exclusive contracts outside the adapter; no unrestricted preemption.
- `x86-irq0.S`: under **only** `SPIFFOS_M10_D59B_CONTEXT_TEST`, increments depth
  after saving all GPRs and before C; decrements after selector before return.
  Existing normal tick and single EOI are unchanged. No allocator in IRQ.
- `memory_boot_d59b_frame_cycle`: two real independently preempted workers use
  the real private boot allocator, with nested authenticated D5.7 protection.
  Checks slot/counter ownership via successful allocate/release, rejected wrong
  owner and forbidden reserve. No numeric physical address is dereferenced or
  task backing freed. Unexpected failure retains exclusion; no blind retry.
- Hosted backend compiled only for native tests; a freestanding build defining
  `SPIFFOS_D59B_HOSTED` fails at preprocessing. It cannot become a kernel backend.
- Source inventory grew from 64 to 65; the new adapter joins explicit allocator
  review entries. Exact changed-source review covers adapter, guest hooks,
  boot-frame wrapper and IRQ assembly. Manifest still says NO_PREEMPTION and
  does not certify unchanged caller chains or the 31 hash-only sources.

## Qwen attempts, constrained escalation and independent verification

Ollama `/home/geoff/.local/share/ki5vnb-coding/ollama-0.40.11/bin/ollama`, API
`http://127.0.0.1:11434`, model `qwen3-coder:30b`, was actually available.

1. Combined adapter draft request timed out after 240 seconds. No code accepted.
2. Split current-owner helper invented fields; a corrected actual-header request
   completed in 28.57 seconds. Coordinator retained its predicate, added explicit
   overflow checks and full contract comments.
3. First core init/enter/exit draft enabled IF on failure, truncated RSP, invented
   includes/fields, and treated full architectural RFLAGS as an IF bit. Rejected.
4. Narrow enter/exit correction compiled with strict GCC/ASan/UBSan, then failed
   the independent hosted test at `positive: d59b_enter(..., inner)` (exit -6).
   It inverted IF nesting rules and supplied false live-IF-clear on exit.
5. Final focused core repair failed strict compilation: shadowed cpu_id,
   wrong current_owner signature, nonexistent task/guard fields. Scratch compiler
   evidence: `/tmp/spiffos-d59b-qwen-final-rejected.log`.

After three unsuccessful core implementations, Codex fallback replaced **only
adapter admission/exit safety logic**, with coordinator CPU/backend and IRQ/test
integration. Qwen's init algorithm was retained with correct IF-mask validation.
Qwen then generated the separate frame-cycle helper; coordinator corrected its
ignored exit result, initialized physical output, removed pointless release
attempts after sticky faults, and added rejected-owner/reserve checks. All
accepted contributions were independently built and tested. No claim that raw
Qwen output passed review. Independent tests and integration were coordinator
work; no parallel Codex implementation of unrelated subsystems.

## Actually executed tests

Commands run on Forge from the isolated workspace:

```sh
make HOST_CC=gcc test-m10-d59b-context-host
make HOST_CC=gcc test-m10-d59b-context-guest
env GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory \
  GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine \
  make HOST_CC=gcc test-m10-d59b-acceptance
```

- Hosted GCC strict C11 ASan/UBSan: PASS. 20,000 real M10-A/D5.2b/M2 nested cycles,
  IF-on/off restoration, stale generations, wrong CPU/stack/IRQ, quarantined and
  terminated current records, corrupt depth and invalid LIFO cookies. Invalid
  entries preserve task/resource state and caller output; sticky fault prevents
  subsequent admission/restoration. Marker `SPIFFOS_M10_D59B_CONTEXT_HOSTED_OK`.
- Strict native Clang non-sanitized: PASS, same tests. Forge's missing Clang ASan
  runtime remains a host limitation; no package changes.
- Strict freestanding Clang `-Wall -Wextra -Werror -Wpedantic`: PASS. Attempting
  hosted-backend injection in the kernel: EXPECTED compilation rejection.
- Actual IRQ0 QEMU 128/256/512 MiB: PASS two independently owned workers,
  original IF=0 and IF=1 outer entries, nested frame transactions, exact IF
  restoration, 220 authentic IRQ observations each, supervisor max gap 3 ticks,
  off-stack cleanup and restored editor boot. Dedicated IRQ admission negative:
  PASS all three sizes, rejected before allocator work/editor with sticky fault.
- Observed full exclusion TSC maxima in the targeted final run:
  128MiB 1,225,531; 256MiB 1,274,731; 512MiB 1,235,330 guest cycles. These span
  post-CLI/pre-restore validation and nested body, excluding the sampling edges.
  They are observed guest cycles, **not calibrated microseconds, exhaustive
  worst-case latency, physical CPU deadlines or service guarantees**.
- Inventory mutation tests updated only for the intentional added source count
  and explicit path; source-drift/false-cue/added-source checks remain required.
- Full inherited acceptance: PASS, terminal exit 0 after 541.79 seconds; final marker `SPIFFOS_M10_D59B_HOSTED_ACCEPTANCE_OK`, including all inherited host/IRQ/memory/editor/keyboard/APIC/MMIO and new context gates.
- Exact-head independent GitHub CI: required after this change is committed;
  never infer success from parent PR #90. Check the draft PR current head.

The first inherited run reached a direct ISO build and stopped at Git's
ownership check on the borrowed owner-owned Limine checkout. The retry uses
only the command-scoped exception above for the independently verified pinned
commit `7a9013f305de0bea1f6310e76e7baba30499fef0`; no global Git, permissions,
ownership or host configuration changed. Initial failure log:
`/tmp/spiffos-d59b-context-full-acceptance.log`.

Final Forge log: `/tmp/spiffos-d59b-context-full-acceptance-final.log`; exit
record: `/tmp/spiffos-d59b-context-full-final-exit.txt`. Targeted final log:
`/tmp/spiffos-d59b-context-targeted-final.log`.

## Compatibility, rollback and remaining assignment

A fresh unflagged build passed exact stripped ELF comparison to accepted D4:
`82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`.
The kernel extracted from the actual accepted ISO independently matched it.
Accepted ISO stayed:
`0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`.
No merge, accepted ISO overwrite, host component/privilege, physical disk,
installation media, network, OPNsense or other project changes.

Rollback: discard this isolated branch and keep owner D4 ISO. No recovery action
on the accepted deployment is needed. The test ISO is NOT the useful M11 preview.

Next executable assignment after this head's CI passes: extend the SAME adapter
and guard to reviewed bounded heap/service and page-zero/rollback transactions;
retain no-task boot exclusion separately; test OOM, metadata exhaustion, mapping
failure/unwind, root publication and referenced-stack prohibition. Measure real
IF-off spans, do not put large payload zeroing in a blanket CLI wrapper. Real
allocator APIs still have legacy direct callers; this narrow fixture does NOT
close bypass/caller coverage, long-operation timing or lifetime gates. Source review identifies a specific timing issue to measure before extending
protection: `page_access_resolve` scans all frame metadata via
`frame_allocator_check` and reanalyzes the map, while `page_access_allocate_zero`
also clears a page before publication. The general heap permits 4MiB arenas and
1024 metadata records, but the live M6 service uses one 4096-byte arena and 64
records. Do not extrapolate the short frame-cycle bound to those operations.
A Qwen draft for the next bounded service cycle exists only in
`/tmp/spiffos-d59b-qwen-next-service.txt`; it is NOT reviewed/installed/accepted.
It requires fixing ignored fault-path exits, explicit OOM sentinel preservation
and checking all 32 payload bytes before use. Then
D5.9-C/D/E and M11 interfaces/integrated preview remain to be implemented and
accepted. No ordinary BASIC/EDIT preemption or ready-to-boot M11 claim.

Change history: 2026-11-15 docs-first scope, Qwen attempts/fallback, authenticated
adapter, actual frame transaction and negative IRQ tests; no release.
