# M6 service core — stopped checkpoint, 2026-10-30

The owner requested the next retained-memory section, then explicitly requested
stop, commit and merge. Development stopped at the service-library boundary.
The earlier 14:00 merge/deployment window had expired; the new instruction
authorizes this concrete merge. It does not authorize a new deployment.

## Implemented and pending
memory-service.h/.c provide a caller-owned service over M4/M5, 64 external
records, ten protected heap roots, const getters, owner allocation/release and
workspace reset generations. Methods validate the complete heap and exact root
block ownership/size before dereferencing root payloads. Ordinary failures
preserve output/storage. Initialization/reset failure paths retire pointers.
Backing is borrowed from the caller; no physical allocation occurs here.

The core is not linked into the kernel or initialized at boot. M6 retained
backing, startup/late verification, guest markers and the complete planned
failure-injection matrix remain future work. No full workspace manager,
page growth, stale-pointer isolation, ring-3 protection or concurrency is claimed.

## Model provenance and interruption
Qwen's one-header job exited 55 after 180.65s. It produced an incomplete layout
that embedded state rather than allocating ten roots and omitted heap/error
contracts. Codex inspected/replaced that scaffold and wrote the 609-line core,
then its CLI hit the account usage limit (exit 1, 271.75s) before compilation,
tests, integration or final report. The dependent task queue correctly did not
start. No background model job remained running at the stop checkpoint.

The coordinator reviewed the saved core, added focused tests and normal Makefile
targets, compiled and checked the source. Applicable references: AGENTS.md,
accepted/open, system overview, ADR-011 and the memory/M6 ADRs. The owner had
authorized direct memory fallback after repeated Qwen limits/timeouts.

## Actual validation
Strict host C11 -Wall -Wextra -Werror with AddressSanitizer and UBSan passed the
focused core test: ten roots/IDs, protected root release, zeroed allocations,
OOM output/state/payload atomicity, whole-service output alias rejection, all
nine resets, other-owner payload preservation, metadata-exhausted reconstruction,
generation overflow unchanged, forged root rejection before dereference, and
independent guard bytes. No sanitizer workaround was used.

Strict x86-64 freestanding Clang compilation at -O2 passed. nm -u showed only
the intended kernel_heap dependencies, with no hosted memset/memcpy requirement.
The normal make test-memory-service target and git diff --check were also checked.
No new QEMU test or image build is claimed: this module is not in the boot link.
The existing deployed M1-M5 image is unchanged. No guest, host service, physical
media, network, firmware or infrastructure was changed. Rollback is main e7120b3;
this source-only checkpoint introduces no running-state migration.
