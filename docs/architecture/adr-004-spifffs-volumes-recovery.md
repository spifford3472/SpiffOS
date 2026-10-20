# ADR-004: SpiffFS, logical volumes, deletion and recovery

Status: **Accepted design direction; implementation details and command syntax remain open**
Date: 2026-10-20
Scope: Documentation only; no implementation authorization.

## Context and goals

SpiffOS is a single-user, BASIC-first native x86-64 OS with nine isolated workspaces and a protected Supervisor. Storage must be understandable, fast, resilient to abrupt power removal, and safe for novice programmers. This ADR records the decisions agreed during filesystem design discussions. It does not specify a complete on-disk format.

## Physical layout and native filesystem

- Use a native copy-on-write **SpiffFS** for user data, with crash-consistent metadata, checksums where appropriate, atomic root/commit transitions and carefully ordered writes.
- Keep the UEFI FAT32 EFI System Partition and independent A/B OS slots separate from the user-data SpiffFS pool.
- Successful file saves must preserve the last committed file if a write is interrupted. A SAVE is reported durable only after appropriate persistence/flush operations, subject to hardware honoring them.
- File and volume lookup is case-insensitive while preserving the original case for display. Unicode normalization and case-folding rules remain open.
- User-facing named logical volumes share available space dynamically in one SpiffFS data pool; they do not each require a fixed-size physical partition.

## Workspace-private and shared volumes

- Nine persistent private volumes, conceptually `W1:` through `W9:`, one per workspace. `W:` refers to the current workspace's private volume.
- Ordinary programs can access their workspace-private volume and shared volumes, not other workspaces' private volumes. Multiple BASIC processes within a workspace may access that workspace's volume while remaining memory-isolated.
- Working directories are per process, with a workspace-level default.
- The protected Supervisor can access all private volumes and perform administrative actions; it is entered with Ctrl+Alt+Delete and is **not** a tenth workspace. Supervisor services continue independently when its UI is hidden.
- Single-user operation does not eliminate Ring 3 process isolation or privilege checks. Ordinary BASIC applications cannot impersonate the Supervisor.
- Shared volumes are visible across all nine workspaces and may be created directly in BASIC, e.g. proposed `VOLUME.CREATE "GAMES"`. Duplicate names fail with `VOLUME ALREADY EXISTS`; reserved names are protected.
- An empty shared volume may be deleted directly from BASIC, e.g. proposed `VOLUME.DELETE "GAMES"`, without a confirmation prompt. A nonempty volume (including subdirectories) can be deleted only in the Supervisor with explicit confirmation.
- Volume deletion checks must be atomic with respect to concurrent activity. Proposed errors include `VOLUME NOT EMPTY`, `VOLUME IN USE`, `VOLUME NOT FOUND` and `PROTECTED VOLUME`. No force-recursive volume deletion in ordinary BASIC.

## File discovery and removable media

- Provide interactive file search (`FINDFILE`) and a callable BASIC search facility (proposed `FILE.FIND`), including optional recursion and volume selection; syntax remains open.
- Support FAT32 USB media, automatic detection, and manual `MOUNT` and `EJECT` commands. Target at least read-only FAT32 support in v1; writable FAT32 after sufficient validation.
- Native SpiffFS removable media is desirable. Provide a Linux-side `spifffs-tool` for creating, formatting and testing filesystem images and USB media during development.
- USB host controller and mass-storage drivers are separate implementation prerequisites; a Linux-side formatting tool does not imply SpiffOS USB support.

## Deleted-file recovery

- Normal file deletion retains a recoverable entry, accessible through proposed `UNDELETE` commands.
- Default retention is **30 days**, configurable, with an optional unlimited setting subject to space pressure.
- Deleted files and overwritten-file versions share a recovery-storage budget of **10% of the SpiffFS data pool** by default.
- When total free space falls **below 8%**, reclaim the **oldest recoverable entries first**, whether deleted files or saved versions, until free space reaches approximately **10%** or no eligible entries remain.
- Routine cleanup also reclaims entries past their configured retention period; exceeding the recovery budget likewise requires reclaiming eligible oldest entries.
- The 30-day retention is a **best-effort target**, not a guarantee under storage pressure. Never delete active files or current file versions to meet recovery thresholds; use ordinary out-of-space behavior if needed.
- Cleanup and UNDELETE must be synchronized to prevent races. Recovery honors workspace-private access restrictions and shared-volume permissions.

## Automatic file versioning

- On successful overwrite, automatically preserve the **three most recent previous versions** of any file type, subject to the **30-day default maximum age**, configurable version count and retention.
- Versions use the same recovery budget and low-space cleanup policy as deleted files.
- Proposed BASIC operations: `FILE.HISTORY "ASTEROIDS"` and `FILE.RESTORE "ASTEROIDS", 2`. Restoring an earlier version itself retains the displaced current version when space/retention policy permits.
- Use copy-on-write block sharing where feasible; rewritten blocks may still consume additional storage. Version history is not a substitute for independent backup.

## Supervisor and security boundary

- The owner uses Ctrl+Alt+Delete for privileged management, including confirmed deletion of populated shared volumes, recovery and system administration as appropriate.
- The Supervisor interface should run in a protected user-mode context using privileged kernel APIs; privileged background services do not depend on the UI being open.
- Physical presence is not equivalent to strong authentication; optional owner PIN remains a future decision.

## Open design questions

1. On-disk COW structures, commit/replay or root-swap scheme, metadata checksum coverage and free-space accounting.
2. Unicode case folding, filename/path limits, reserved volume names and volume rename semantics.
3. Exact BASIC command syntax, return values, errors and programmatic APIs for search, recovery and version history.
4. Trash/version record placement, retention accounting, deletion timestamp rules and behavior on renamed/moved files.
5. Atomicity and consistency for concurrent file writes, rename, restore, volume deletion and background cleanup.
6. Retention configuration surface, capacity reporting and safe behavior if recovery entries cannot reclaim sufficient space.
7. FAT32 writable support, removable-media safety, and initial hardware driver coverage.
8. Backup/restore and integrity-check utilities, including recovery from filesystem metadata damage.

## Non-goals

This ADR does not authorize code changes, finalize an on-disk binary format, promise recovery after physical media failure, or provide C64 disk/binary compatibility.
