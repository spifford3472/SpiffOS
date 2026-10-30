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

### M3 direct-map and page-access contract
The repository keeps the unchanged Limine protocol header from commit
`3a0526b700e356f0eac1b71a77697b3fd1c707a3` (recorded SHA-256
`0aadf2633c85d8cb145344c968add158aeb1deb68af567d55ba8431c492b6d4c`)
and the binary checkout at v10.8.5 commit
`7a9013f305de0bea1f6310e76e7baba30499fef0`. The header declares revision-0
HHDM and paging-mode requests. The binary/header pair has already accepted
protocol base revision 5 in QEMU; M1 also proved revision-0 request/response
operation for the memory map. These local facts establish the request ABI and
the exact boot pair to test. They do not justify assuming an HHDM response on
every machine or treating an unvalidated offset as a pointer.

The boot adapter will request both HHDM and x86-64 paging mode at revision 0.
It accepts only non-NULL revision-0 responses and the pinned header's explicit
4-level or 5-level x86-64 modes. Those modes select 48-bit or 57-bit canonical
virtual-address checks respectively. Any absent response, newer response
revision, unknown mode or rejected base revision makes M3 unavailable while M1,
M2 and the editor continue. There is no identity-map fallback and no inference
from the kernel's template-derived higher-half link address.

`page_access_resolve()` is pure policy. Its caller supplies an initialized M2
allocator, a validated HHDM offset/address width, one aligned physical page and
the exact non-FREE owner. Before publishing a virtual address it must prove:

- the allocator metadata is internally consistent and the physical page maps
  to exactly one complete M1 USABLE candidate frame;
- that frame's live ownership slot equals the requested owner (including the
  supervisor rule for reserve ordinals);
- the 4096-byte physical and virtual half-open ranges do not overflow;
- `physical + hhdm_offset` and its last byte are canonical for the reported
  address width.

Failure preserves the caller's output and all allocator state. A successful
numeric virtual address is an authorization result for the narrow M3 backend,
not a general pointer or a promise about future page tables. The policy has no
Limine dependency so host tests can inject offsets, widths, maps and ownership.

`page_access_zero()` is the only initial content operation in this checkpoint.
It first resolves the page, then invokes an injected synchronous backend for
exactly 4096 bytes. The kernel backend alone converts the approved address to a
pointer and uses byte stores; host tests use guarded ordinary RAM. Backend
failure is explicit. The higher-level allocate-and-zero operation must release
the just-allocated frame with the same owner if resolve/zero/verification fails,
and must not publish the address until all steps succeed. A later release and
reallocation must zero the complete page again. M3 does not clear arbitrary
free pages, reclaim loader memory, alter CR3, or create mappings.

The descriptor, allocator map and ownership slots are borrowed for the call and
remain exclusively owned by the boot CPU with interrupts disabled. No SMP,
interrupt-context use, locks or stale-handle protection is claimed. Runtime
acceptance still requires actual 128/256/512 MiB boots proving the returned HHDM
range can be written, read, released, reallocated and observed zero without
changing editor pixels; the declarations and prior base-revision evidence alone
are deliberately not recorded as that runtime proof.

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
