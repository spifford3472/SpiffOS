# D5.9-B retained heap/service transaction evidence and handoff

Date: 2026-11-15. Isolated continuation; no release or M11 completion claim.
Branch: `feat/m10-d59b-service-transactions`. Parent PR #91 at
`aabbea5b9ba3676f2a588e2db3ff2af7e3befa72` passed exact-head independent
GitHub CI run #108, ID `43685056396`. Parent full Forge acceptance also passed.

## Design, source and authority

Owner autonomous D5.9-B scope authorizes this continuation. Governing sources:
AGENTS.md, accepted/open registers, system overview, developer guide,
ADR-m10-d59b-authentic-context.md, ADR-m10-d59b-service-transactions.md,
ADR-memory-services.md, ADR-persistent-memory-service.md, and actual M4/M6 APIs.
Docs-first ADR committed as Forge `3eddcbd` before installing runtime candidate; its byte-identical GitHub docs-first tree was published as `729ae86a26480b3cf8bcf69a4f09d49e6d979352`. Publishing uses the connected GitHub API because shell HTTPS push has no credentials; no credential installation or host configuration change.

There is still ONE existing memory_service/kernel_heap implementation, ONE
D5.7 guard and ONE M10-A task table. The new `m10-d59b-service.[ch]` is a
**diagnostic probe**, not another allocator or a general allocation API.
Both hosted tests and the real boot service call the identical probe code.

The probe first authenticates CPU/IF/IRQ/current generation/stack through the
same D5.9-B adapter. Before traversing service metadata it requires the actual
one-page 4096-byte arena and 64-record bound. It allocates exactly 32 bytes,
checks zeroing, writes/checks every payload byte, rejects another workspace's
release, observes recoverable OOM with an unchanged non-NULL output marker,
rejects root release, frees only its own client and verifies complete accounting.
No client pointer escapes the guard; no reset/retire, IRQ allocation, callbacks,
logging, second lock or forced recovery is introduced. An unexpected error
latches D5.7 failure and retains IF=0 without repairing or releasing unknown state.

The two existing independently stacked M10-A workers invoke the probe against
`retained_service` through a narrow memory-boot bridge under the additional
`SPIFFOS_M10_D59B_SERVICE_TEST` flag. Frame transactions, genuine IRQ0 scheduling,
original IF on/off tests, supervisor service, off-stack cleanup and restored
editor remain inherited. No normal unflagged kernel service is made preemptible.

## Model provenance and review

Local Ollama `qwen3-coder:30b` generated only the small service-cycle draft.
The coordinator corrected fault paths that tried to exit after sticky failure,
expanded payload preservation from one byte to all 32, used a non-NULL OOM
sentinel, checked final exit, admitted only the measured descriptor size, and
adapted the same probe to a borrowed real instance. The draft was first compiled
and tested outside the repository (20,000 native sanitizer cycles), then copied
into the docs-first child branch after review. No unrestricted model rewrite or
new subsystem implementation occurred. Independent test/integration code was
coordinator work; the prior limited core-adapter Codex fallback is unchanged.

## Tests actually executed

```sh
make HOST_CC=gcc test-m10-d59b-context-host
make HOST_CC=gcc test-m10-d59b-service-host
make HOST_CC=gcc test-m10-d59b-service-guest
env GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory \
  GIT_CONFIG_VALUE_0=/home/geoff/projects/spiffos/.local-dev/limine \
  make HOST_CC=gcc test-m10-d59b-acceptance
```

- Native GCC strict C11 ASan/UBSan: PASS `SPIFFOS_M10_D59B_SERVICE_HOSTED_OK`.
  20,000 cycles cross both workspace owners and both original IF states,
  preserving all ten roots and exact M10-A/resource state. Negative cases
  include corrupt counters/root ID, unsupported descriptor bounds, retired
  service, IRQ context, wrong CPU/RSP, null service and invalid owner index.
  Rejections preserve every service/arena byte and latch exclusion where admitted.
- Shared `tests/m10-d59b-test-cpu.h` contains only the previous test CPU setup;
  every existing context-test assertion remains. Parent 20,000-cycle context
  sanitizer suite: PASS after the extraction.
- Native Clang strict C11 non-sanitized: PASS same new service tests. Strict
  freestanding Clang compilation: PASS. Clang sanitizer runtime was not installed.
- Real QEMU 128/256/512MiB: PASS. In the first targeted run both workers completed
  service cycles [154,731], [152,726], [152,722] respectively; each also completed
  its real frame cycle. All owner/OOM/root/payload/accounting checks passed,
  220 genuine IRQ entries per boot and maximum supervisor gap 3 timer ticks.
  Initial IF=0 and IF=1 outer calls and nested service exclusion were preserved.
- Observed full exclusion maxima in that run: 2,683,778 / 2,649,584 / 3,172,662
  guest TSC cycles. These cover the combined short frame/service transaction;
  they are not calibrated physical microseconds or exhaustive worst-case bounds.
- The actual-IRQ admission negative remains mandatory at all three sizes;
  a rejected context cannot reach either frame or service allocator work.
- Full inherited Forge acceptance: PASS, terminal exit 0 after 576.67 seconds; final marker `SPIFFOS_M10_D59B_HOSTED_ACCEPTANCE_OK`, including all inherited gates and both new context/service hardware suites.
- Default ELF/accepted ISO comparison: PASS, fresh default stripped ELF is D4-identical and owner accepted ISO SHA remains unchanged.
- Independent exact-head child CI remains required after publication. Do not
  infer child acceptance merely because parent run #108 passed.

Logs: `/tmp/spiffos-d59b-service-targeted.log`,
`/tmp/spiffos-d59b-service-full-acceptance.log`,
`/tmp/spiffos-d59b-service-full-exit.txt`.
Source inventory expands to 66 C/ASM files; new probe joins explicit allocator
review entries. Changed-source review covers the probe, worker hook and boot
bridge. All NO_PREEMPTION states and 31 unreviewed source entries remain intact.

## Remaining work and exact next assignment

This closes only the bounded retained-service transaction proof. Ordinary
legacy callers still bypass the candidate. General 4MiB heaps/1024 records,
large zeroing, root reset/retire, page mapping/rollback and executing/referenced
stack lifetime must NOT be declared protected by this 32-byte probe.

Next inspect `page-access.c` (`page_access_resolve`, `page_access_allocate_zero`),
`memory-boot.c` page backends, `kernel-task-resources.[ch]` and the actual root
callers. In particular page resolution currently scans every frame and
reanalyzes the memory map under an exclusive-call contract. Measure that span
before wiring it into runtime workers; do not mask IRQs blindly around arbitrary
size operations. Plan private initialization versus metadata publication and
ensure the existing resource ledger prevents cleanup of an in-flight/private
page or active stack. Do not introduce a parallel ownership registry, force
orphan cleanup, add allocation to IRQ or bypass D5.7 generation validation.

After the remaining D5.9-B memory/caller/lifetime gates, continue D5.9-C/D/E and
M11's integrated architecture/preview under the original owner scope. Full
BASIC/graphics/audio/VFS functionality is still not implemented by this patch.

## Rollback and change history

Discard this child branch; parent #91 and accepted D4 remain recoverable.
Accepted D4 ISO must stay `0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`;
default stripped ELF must stay `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`.
No merge, deployment, physical disk/media, host package/privilege, infrastructure,
firewall or other-project changes. No owner interactive preview is ready yet.

2026-11-15: docs-first bounded continuation, Qwen draft/review, shared hosted
fixture and genuine retained-boot-service tests. Acceptance dependencies above
remain explicit; no general kernel preemption claim.
