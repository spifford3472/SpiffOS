# ADR M9-34 — Low-memory boot and owned workspace snapshot pages

Status: approved M9 remediation scope, implementation under validation (2026-11-11).

## Problem and evidence
On OVMF/q35 QEMU, 128/160 MiB fails before kernel entry with Limine's `High memory allocator: Out of memory`. The kernel has ~47.8 MiB BSS: nine 1280x800x4 fixed raw framebuffer snapshots are 36.9 MiB, while fixed 16-GiB-capable frame metadata consumes 8 MiB. 192 MiB and higher boot. A disposable experiment with only one snapshot and reduced owner-slot capacity passed 128 MiB, demonstrating reducing loader-time committed zero storage is relevant. The previous split-PT_LOAD experiment did not resolve the failure.

## Internal M9 remediation
Retain the same nine 1280x800 workspaces, complete raw snapshot pixel fidelity, M6 session root authority, 64-page supervisor reserve and F1–F12 behavior. Replace the nine fixed snapshot buffers with page-sized ordinary frame allocations made lazily through the existing frame allocator and validated HHDM. Before mutating the framebuffer, reserve every required page for that workspace or reject with no active-ID/pixel changes. On failure, release only the frames allocated by that attempt. Existing snapshots remain owned until shutdown; workspace reset invalidates its saved image but does not release backing until a future explicit resource-lifetime service.

To reduce loader BSS without reducing existing candidate-frame capacity, use compact one-byte owner tokens rather than two-byte tokens. All current enum values fit in uint8_t. Keep the existing 4194304 slot cap (16 GiB of candidate frames), protected reserve, explicit unavailable behavior, and allocation semantics. This is an internal M2 metadata storage change, not a public ABI.

## Constraints and rollback
No new paging tables, heap fallback, ring 3, IRQ enable, physical installation or network changes. Every successful mapped page is attributed to a workspace owner and must be page-access authorized. Maintain old failure-safe behavior when allocation is unavailable. Hosted tests inject out-of-memory/partial failure and independently verify all nine pixel snapshots. Guest tests cover 128/256/512 MiB, exact framebuffer pixels, all function keys, F12 measured duration and serial markers. The previous working ISO is retained for rollback and no merge occurs until all gates pass.


## Improvement after first passing test — compact owner slots
The first implementation used 262144 two-byte slots (1GiB of frame tracking), which passed 128/256/512 MiB but would regress prior 16GiB bookkeeping capacity. The preferred correction retains **4194304 slots** and changes their type to uint8_t: the 13 current owner values fit in a byte. This preserves the original 16GiB candidate-frame count and halves owner metadata BSS from 8MiB to 4MiB. The internal M2 frame allocator and every hosted test fixture must use the same representation; this is not a public ABI. Reject future owner IDs that do not fit in uint8_t at compile time. No final RAM ceiling is newly introduced by this fix.
