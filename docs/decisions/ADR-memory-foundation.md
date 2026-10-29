# ADR — Memory foundation and read-only inventory

Status: ACCEPTED scoped direction and M1 implementation authorization,
2026-10-29. The M2 allocator proposal below is superseded by the implemented
[ADR-frame-allocator](ADR-frame-allocator.md); later heap/mapping interfaces
remain PROPOSED.
Read with ADR-011, ADR-002, ADR-008, accepted/open and AGENTS.md.

## Context and ownership
The owner authorized memory foundations for the OS, system session and nine
workspaces, then explicitly allowed direct work and Codex fallback when Qwen
times out, requesting very small Qwen jobs. Two initial documentation passes
timed out after 480s and 300s with no edits; smaller jobs are required.

One authoritative memory manager will manage a shared RAM pool, rather than
nine equally sized fixed workspace partitions. Existing ADR-011 already lets
workspaces draw from available RAM. Ownership/accounting is distinct from
hardware protection: tags alone cannot enforce ring-3 isolation.

| Owner category | Intended lifetime and contents |
|---|---|
| OS/kernel | Kernel, drivers, allocator metadata and essential services |
| System session | Shared session state; exact service/process layout open |
| Workspace 1–9 | Workspace state and allocations attributed to its processes |
| Supervisor reserve | Protected recovery resources unavailable to ordinary demand |

The system-session category does not create a tenth workspace or prescribe a
new privileged process. Supervisor recovery stays independent of BASIC.
RAM-only/no swap, recoverable OOM and no automatic OOM killing remain accepted.
Reserve sizing, process/workspace limits and accounting APIs remain OPEN.

## M1 — Implement now: inventory only
Request the pinned Limine revision-0 memory map without changing its loader,
header, protocol base revision 5, entry stack or page tables. Snapshot at most
512 entries in static kernel storage. Explicitly reject missing pointers,
unsupported response revisions, empty/oversized maps and overflowing ranges;
never silently truncate. Zero-length entries contribute no memory.

Validate all intervals before counting anything. Reject overlap involving a
USABLE interval, including duplicate usable intervals; permit overlaps solely
between non-usable types, as the Limine protocol permits them. Input order
does not affect the result. Do not modify borrowed bootloader data.

Only USABLE regions contribute complete aligned 4096-byte candidate frames.
Align inward using division/remainder without overflowing an align-up sum.
Exclude physical frame zero explicitly as an initial safety policy. Count:
raw usable bytes, complete usable frame count, candidate frame bytes, and
usable bytes excluded by alignment/frame-zero policy. Validate their arithmetic.
These are inventory counters, not installed RAM or allocated/free heap memory.
Never sum overlapping reserved/MMIO lengths into a purported installed total.

All other/unknown types stay unavailable: kernel/modules, framebuffer, ACPI,
firmware, bad memory and bootloader-reclaimable data. The current stack, page
tables and protocol responses may still occupy bootloader-reclaimable memory.
Do not reclaim them. No physical frames are allocated, freed, zeroed or written.
No HHDM offset, CR3 layout or process address space is established in M1.

Emit counted per-entry and summary serial diagnostics, ending in
SPIFFOS_MEMORY_OK only after complete validation and output. On an unavailable
or malformed map emit SPIFFOS_MEMORY_UNAVAILABLE and continue the existing
boot/editor path where possible. The on-screen framebuffer remains unchanged.
A UART output failure cannot produce a false complete success marker.

## Following checkpoints — proposed, separately reviewed
Historical M2 proposal (superseded by ADR-frame-allocator): 4KiB frame
allocation/free and explicit owner/error accounting. A bitmap
is a proposed simple choice: bounded bit storage, straightforward validation;
free lists write metadata into frames and range lists complicate splitting.
Choose allocator metadata placement and capacity before implementation.
Reject invalid/double/wrong-owner frees; ordinary allocation cannot consume the
supervisor reserve. Preserve state on recoverable OOM. Physical address zero
must not serve as an ambiguous success/failure pointer sentinel.

M3: validate mappings/HHDM access before frame writes, then kernel heap,
page tables and fault handling. Zero pages before exposing them to user code.
M4: protected process spaces and real session/workspace lifecycle accounting,
including reclamation on explicit process teardown. The current static editor
is one kernel diagnostic, not nine workspaces or a BASIC runtime.

## Validation and rollback
Host ASan/UBSan tests: holes, unsorted entries, alignment, frame zero, zero
length, missing inputs, bounds/overflow, unknown types, duplicate/overlapping
usable regions and legitimate reserved-only overlaps. Failure leaves caller
summary unchanged; input storage is borrowed and unchanged.

QEMU at 128/256/512MiB: require boot/input readiness, inventory success, exact
summary recomputation from serial entries and growth of usable capacity.
Retain all existing editor screenshots, workspace/forced-scroll and launcher
checks. No tests are claimed by this design record; actual results belong in
the operations record. Later tests cover allocation exhaustion, reserve access,
ownership, zeroing and teardown.

Work in isolated feat/memory-foundation from 9e98a0f. Preserve the owner index,
drafts, deployed main and branding assets. Prepare a reviewable PR; no new merge,
physical deployment or infrastructure authorization is inferred. Rollback is an
isolated build of 9e98a0f.

Protocol reference: https://github.com/limine-bootloader/limine-protocol/blob/trunk/PROTOCOL.md
The current upstream document was checked for memory-map semantics; no protocol
upgrade is implied. Implementation uses the repository's pinned header.
