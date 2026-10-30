# ADR — M6 retained memory service and state lifetimes

Status: scoped implementation authorized 2026-10-30 when the owner requested
continuing the next section after M3–M5. Reviewed local QEMU merge/deployment
permission lasts through 14:00 Eastern today (18:00 UTC). No physical hardware,
firmware, infrastructure, page-table rewrite or user ABI is included.
Read AGENTS, accepted/open, system overview, ADR-011, ADR-memory-foundation,
ADR-frame-allocator and ADR-memory-services. Existing owner/reserve rules apply.

## Decision and rationale
Retain one ordinary kernel-owned 4096-byte M3-validated page after boot and
provide a small caller-owned memory-service layer over M4/M5. The service has
64 external block records in kernel storage, enough for its ten state roots
plus bounded client allocations. Its arena remains ordinary kernel backing;
workspace subownership never changes physical-page ownership or touches reserve.
A larger multi-page pool, growth and page return policies are future decisions.

Each service contains one heap-allocated session state and nine heap-allocated
workspace states. Session state records workspace count and active workspace ID
(initially 1); workspace state records its immutable ID and reset generation
(initially 1). These are retained identity/lifetime records, not a scheduler,
complete workspace manager, editable screen migration or BASIC environments.
Only service methods mutate identity/lifetime fields; getters borrow const state.

Expose bounded internal initialize/check, allocate/release, workspace reset,
read-only session/workspace access and statistics as needed. Use explicit errors,
16-byte alignment and zero-before-publication inherited from M4. Validate output
storage against the whole service and arena, not only the embedded heap fields.
Reject an ordinary release of any service-owned state-root pointer. Validate a
root's live exact-owner block and size before dereferencing it. Caller keeps all
service/arena/metadata storage stable and disjoint; one CPU, interrupts disabled.

A workspace reset checks generation overflow and service invariants first,
releases that workspace's allocations, then reconstructs its state root with the
same ID and incremented generation. Other roots, client payloads and counts must
be preserved. The previous valid root guarantees enough space/metadata to
reconstruct a root; prove that property in tests. Unexpected reconstruction
failure retires the service and cannot publish a success or dangling root.
Generation is a lifetime counter, not a stale-pointer protection mechanism.
Session teardown remains separate; M6 does not expose teardown while retaining
service roots. Do not silently reuse raw M5 session cleanup through this service.

Initialization requires a zeroed unused service and at least one page of valid
accessible backing. No root is published until all ten exist and check succeeds.
If initialization fails after mutation, retire all descriptor/root pointers
before the boot adapter returns backing to M2; partial arena bytes need not be
preserved because backing has never been published. Ordinary allocation/free
errors preserve state/output and payloads. Corrupt state requires retirement,
not speculative repair. The retained boot service lasts until shutdown/reset.

## Boot integration and evidence
Leave M1–M5 reversible probes intact. Afterwards initialize M6 from one ordinary
M3 page. Exercise extra allocations, wrong-owner/root-release rejection, workspace
reset and preservation of a second workspace's live payload, then release only
extra test blocks. Retain the session and nine workspace roots and backing.
Emit MEMORY_SERVICE:4096,1,1,9,ordinary_free,reserve_free and
SPIFFOS_MEMORY_SERVICE_OK after checking counters. Verify retained roots and
exact backing ownership again after framebuffer/editor setup and emit
SPIFFOS_MEMORY_RETAINED_OK before BOOT_OK/keyboard readiness. Missing M3 support
emits UNAVAILABLE and keeps the existing editor path. No screen changes.

Tests: guarded host RAM, init bounds/aliases, zeroing, OOM and metadata pressure,
root-release protection, all nine resets, generation overflow, reconstruction,
invalid owners/IDs/output aliases, corrupt root rejection before dereference,
and unchanged other-owner payloads. Real QEMU 128/256/512 MiB checks all M1–M6
markers, exactly one fewer ordinary free frame, untouched 64-page reserve and
existing full-frame pixels. Retain keyboard/reflow and launcher cleanup tests.
Rollback is M5 merge 401d92c (docs main e7120b3). Record model provenance and
actual commands/results separately; this ADR claims no implementation or tests.
