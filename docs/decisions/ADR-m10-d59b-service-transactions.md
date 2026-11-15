# D5.9-B bounded heap/service transaction continuation

Status: owner-authorized isolated development, 2026-11-15; not runtime acceptance.
Parent: draft PR #91, aabbea5b9ba3676f2a588e2db3ff2af7e3befa72. Parent has full
Forge acceptance and exact-head independent CI run #108 SUCCESS. This child
still requires independent acceptance. The accepted D4 owner ISO remains untouched.

Read AGENTS.md, accepted/open registers, system-overview, DEVELOPERS_GUIDE,
ADR-m10-d59b-authentic-context.md, ADR-memory-services.md,
ADR-persistent-memory-service.md, and the real memory-service/kernel-heap APIs.

## Scope and rationale

Reuse the SAME D5.9-B CPU adapter, authoritative D5.7 guard, M10-A task table
and existing memory_service/kernel_heap implementation. Do not create another
heap, service owner, task registry or lock. A diagnostic probe operates on a
borrowed memory_service instance; hosted tests use a real isolated instance,
and QEMU uses the actual retained boot service through a narrow bridge.

The probe allocates and zero-validates exactly 32 bytes for one of two workspace
owners, checks rejected wrong-owner/root releases, observes recoverable OOM
with preserved output, releases its own allocation and proves accounting/root
invariants. Client pointers never escape the guarded diagnostic call. Both real
workers exercise it under the existing genuine timer-driven preemption fixture.

Only the one-page M6 service with 64 metadata records is measured here. This
must NOT become blanket IRQ masking around the general 4MiB/1024-record heap,
large payload zeroing or workspace resets. Read-side full checks must use the
same gate; no IRQ reads, allocation, logs, cleanup, or extra EOI are introduced.
Unexpected corruption permanently faults and retains exclusion; no forced
unlock, retry, owner reset or active-stack release.

## Validation and boundaries

Strict hosted GCC ASan/UBSan and Clang, repeated IF-on/off transactions,
all ten roots preserved, exact owner accounting, OOM output nonpublication,
wrong-owner/root rejection, corrupt metadata and IRQ-context refusal. A shared
hosted CPU fixture may replace duplicated test setup while preserving every
existing assertion. It is absent from the kernel build.

Require separately flagged real QEMU at 128/256/512 MiB, measured exclusion
cycles, independently executing workers and supervisor service, full inherited
acceptance, source-drift checks, exact-head CI, unchanged default ELF/owner ISO.
No general service/heap caller certification, arbitrary transaction sizes,
root reset/retirement, page zero/rollback, physical deadline or M11 completion.

Rollback: discard this isolated child branch; parent #91 and accepted D4 remain
available. No merge, deployment, privilege, host package, infrastructure,
firewall, physical disk or unrestricted ordinary preemption authorization.

Change history: 2026-11-15 documented narrow continuation before runtime edits.
