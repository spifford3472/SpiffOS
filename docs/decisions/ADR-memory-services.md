# ADR — Memory services work package M3–M5

Status: scoped implementation authorized by the owner on 2026-10-29 after
M2, followed by a request for a larger goal with at least three hours of work.
Local QEMU merge/deployment authority expires 2026-10-30 03:00 Eastern
(07:00 UTC); no physical, firmware, disk or infrastructure deployment.
This docs-first phase defines the next independently validated checkpoints.
Read AGENTS.md, accepted/open, system overview, ADR-011, ADR-memory-foundation
and ADR-frame-allocator. The implemented M2 owner/reserve contracts continue.

## M3: checked kernel page access and zeroing
Use an explicitly validated bootloader direct-map response where supported by
the pinned Limine pair. Do not invent an identity mapping or dereference numeric
physical addresses directly. Validate arithmetic, page boundaries, supported
canonical address width and full usable-region membership before access.
Restrict access to pages currently allocated to the exact internal owner.
Separate pure range/translation policy from the narrow boot/hardware adapter.
Zero the complete page before publishing a newly usable allocation. Roll back
ownership on mapping/initialization failure; do not leak partial allocations.
Do not reclaim bootloader memory, write MMIO, change CR3/page tables or expose
ring-3 memory. Missing direct-map support must preserve the existing boot/editor.

## M4: small understandable kernel heap
Build a bounded internal allocator over validated owned/zeroed backing memory.
Document chosen alignment, minimum size, supported maximum allocation and
metadata bounds before coding. Prefer simple first fit and split/coalesce over
sophisticated allocation schemes; prove overflow and adjacent-block handling.
Provide explicit failure, reject invalid/double/wrong-owner frees and preserve
counters/output on errors. Never consume supervisor reserve on ordinary demand.
Track kernel/session/workspace groups. No user ABI, quotas or final VM design
is established. Do not silently replace existing static editor storage yet.

## M5: ownership lifecycle and diagnostics
Provide a tested internal owner-scoped cleanup operation for workspace reset.
It releases only that workspace's allocations, leaving other workspaces, kernel,
session and reserve intact. Session teardown is an explicitly separate lifetime.
Expose allocation/free/owner counters to serial diagnostics and host checks.
These are memory services, not nine running BASIC workspaces or process isolation.

## Alternatives and limits
Continuing accounting-only cannot back dynamic state. A full VM rewrite would
couple too many unresolved interfaces. Borrowing the validated existing direct
map first permits a small access adapter; native mappings and ring-3 protection
remain future milestones. An arena that cannot free would not meet workspace
cleanup; complex slab/buddy designs are unnecessary before this bounded heap.
Owner tags are trusted internal bookkeeping; stale-handle generations and SMP/
interrupt synchronization remain deferred and must be documented honestly.

## Acceptance and execution
Each checkpoint needs host ASan/UBSan adversarial tests, independent counter
checks, real QEMU boot at 128/256/512 MiB and retained editor/launcher regressions.
Prove actual zeroing/reuse and workspace cleanup in guest RAM, not only success
markers. Preserve guard bytes and disjoint owners in host tests. Test OOM,
fragmentation, overflow, wrong ownership and rollback. Keep exact evidence,
model provenance, tested commit hashes and limitations in the operations record.

A persistent isolated Forge worker has a three-hour engineering budget, with
bounded small Qwen attempts and Codex fallback. It must record honest progress
and stop on blockers rather than idle to fill time. It may prepare commits and
push only its feature branch. Runtime merge/deployment follows review of concrete
tested results under the existing owner authority. No autonomous main pushes.
