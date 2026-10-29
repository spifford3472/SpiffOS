# Memory foundation M1 — read-only inventory — 2026-10-29

## Scope and authorization
The owner authorized memory foundations for OS, system session and workspaces,
then direct work/Codex fallback when Qwen times out, with very small Qwen jobs.
The docs-first ADR and registers were committed as 91bbeff before source work.
See ../decisions/ADR-memory-foundation.md for approved scope and later proposals.

Worktree: /home/geoff/projects/spiffos-memory, feat/memory-foundation,
from deployed/documented main 9e98a0f. Original /home/geoff/projects/spiffos
staging/drafts remain separate. M1 merge/deployment is recorded below.

## Implementation
memory-map.h/.c is a pure interval analyzer with a 512-entry diagnostic bound.
Only USABLE memory contributes complete 4096-byte candidate frames; physical
frame zero is excluded. All other types remain unavailable. Whole-map overflow
validation precedes overlap comparisons. Usable overlap is rejected while
reserved-only overlap is allowed. Output counters are transactional on error.

memory-boot.c snapshots borrowed Limine map metadata in static BSS, reports
entries and exact counters to serial, then emits SPIFFOS_MEMORY_OK. Invalid
metadata emits SPIFFOS_MEMORY_UNAVAILABLE and lets the current boot continue.
Writer failure propagates without a later complete success marker. No physical
frames are accessed, allocated or reclaimed; loader stack/tables/responses stay
reserved. Existing header/binary pins and protocol base revision 5 are retained.

qemu.py gains bounded --memory-mib selection, default 256. The 64..4096 runner
range is a test-tool bound, not an OS minimum-memory commitment. test-memory.py
independently computes inventory counters from serial metadata using Python
integers and checks guest boot/input readiness at 128/256/512MiB.

## Model provenance
Local Qwen: 0.26.1, qwen3-coder:30b via existing loopback Ollama, verified
workspace-write sandbox, command network closed, shared coding-worker lock.
No model install, service reconfiguration or infrastructure change.

The two initial docs jobs exhausted 480s and 300s with no edits. A one-module
180s job also timed out with no module. The one-module retry produced
memory-map.c in 152.61s (exit 0). Its first host test failed excluded-byte
accounting. Review additionally identified premature summary writes,
overflow-prone align-up and comparing not-yet-validated later intervals.
A focused correction was assigned back to Qwen for this same module; it
reached its turn limit in 139.39s (exit 53) without changing the function.
Codex corrected this isolated module with staged output, separate whole-map
validation and frame-number arithmetic, then ran the tests below. Codex also
coordinated the design, interface, boot/runner integration, tests and review.

Logs retained in build/: memory-design-qwen.log,
memory-design-qwen-retry.log, memory-map-qwen.log,
memory-map-qwen-retry.log and memory-map-qwen-fix.log.

## Validation status
make test-memory completed with exit 0: host AddressSanitizer and
UndefinedBehaviorSanitizer checks passed for unsorted maps, holes, short and
unaligned ranges, frame zero, zero lengths, null/oversized metadata, arithmetic
overflow, duplicate/overlapping usable intervals, reserved-only overlaps,
unknown types, the 512-entry boundary and unchanged summaries on failure.
The serial adapter tests covered missing/bad response metadata and failure of
every output callback position without a later MEMORY_OK marker.

Actual independent QEMU inventory checks:

| Guest RAM MiB | Map entries | Candidate frames | Candidate bytes |
|---|---:|---:|---:|
| 128 | 32 | 20996 | 85999616 |
| 256 | 32 | 53764 | 220217344 |
| 512 | 32 | 119300 | 488652800 |

Each guest reached SPIFFOS_BOOT_OK and SPIFFOS_KEYBOARD_READY. All counters
matched independent recomputation from its complete serial region list.
These are candidate usable frames, not all installed RAM or allocated heap.
Bootloader-reclaimable memory remains withheld.

make test-keyboard test-workspace test-workspace-scroll completed with exit 0:
strict freestanding build, all previous host sanitizer tests, 356 real QEMU
accepted text bytes/100 named events, all five 1280x800 editor captures and
normal/forced-scroll boot frames. Existing pixels matched without changing
their expectations. python3 -m py_compile passed for both changed scripts.
git diff --check passed.

Evidence: build/memory-validation.log, memory-regression.log,
memory-inventory-results.json, memory-{128,256,512}-serial.log and corresponding
-run.log files. python3 scripts/test-safe-launcher.py completed with exit 0:
real boot, absolute-pointer selection, editor injection/captures, lock rejection,
QMP exit and SIGTERM child/private-resource cleanup passed. Evidence is in
build/memory-launcher.log. Manual GTK resize/focus/release/close observation
remains a separate acceptance item from the earlier launcher milestone.

## Limits and rollback
M1 provides candidate-capacity inventory, not dynamic allocation, a kernel heap,
owner quotas, an emergency reserve allocation, process protection, BASIC or
nine running workspaces. Those follow as separate verified checkpoints.
Rollback: build 9e98a0f in an isolated worktree. Preserve the owner index and
history. No USB, physical disk, firmware, network or infrastructure change.

## PR18 merge and deployment

The owner authorized local QEMU merges/deployments as needed until
2026-10-30 03:00 America/New_York. PR18 tested head
2aa9c9090fec21c6f49b8bc6af9401b8e82ebdbb merged as
81d457bcb140b976be9bedc8b2bbe4dbd3a1c028.

The main checkout rebuilt successfully, but its test runner correctly refused
a lock held by the owner's open QEMU window (PID 1022980). That window was
preserved. A detached checkout /home/geoff/projects/spiffos-pr18-validation at
the exact merge then passed make test-memory test-keyboard test-workspace
test-workspace-scroll (exit 0, 37.37s) and scripts/test-safe-launcher.py
(exit 0, 26.56s). Logs are retained under spiffos-memory/build:
pr18-isolated-postmerge.log and pr18-isolated-launcher.log.
The earlier main-checkout lock refusal remains in build/pr18-postmerge.log.

The tested ISO was atomically installed as
/home/geoff/projects/spiffos-qemu-main/build/spiffos.iso; both ISO hashes matched:
a3a941ce4618de6ec238db80b8667b2ccf69867e445261f9af2779decd955c68
The running guest retains its already-booted RAM state; the new image is used
at the next launch. No running emulator was terminated or physical media used.
