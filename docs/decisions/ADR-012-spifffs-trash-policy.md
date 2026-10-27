# ADR-012 — SpiffFS deletion, versioning and crash consistency

Status: **ACCEPTED policy** by project owner, 2026-10-26. On-disk implementation details remain OPEN. Supersedes earlier automatic deleted-file retention and previous-version requirements where they conflict.

## Context
Earlier SpiffFS drafts specified automatic retention periods and multiple saved versions. The owner prefers a smaller, understandable OS with manual control over deleted files and storage. Recovery from unexpected power loss remains a separate foundational requirement.

## Decision
- **One current saved version per file.** Do not automatically preserve prior saved versions or silently accumulate file history.
- **Deleted files go to Trash** for user-managed recovery. Files remain there until the user explicitly empties Trash or otherwise explicitly deletes them permanently.
- **No automatic Trash expiration or background purge policy** in the initial design. If Trash fills storage, report a clear space error rather than silently deleting the user's data.
- **Crash consistency remains mandatory**: a sudden loss of power must not routinely corrupt the filesystem or prevent boot. Design atomic metadata/file update behavior, recovery, and safe persistence semantics separately from Trash.
- This does **not** promise recovery of unsaved edits, power-loss durability for data not yet committed, or protection from storage hardware failure.
- FAT32 removable media may not offer the same guarantees as native SpiffFS; document those limitations when implemented.

## Alternatives considered
1. Automatic historical versions and time-based deleted-file retention: convenient rollback but consumes space and complicates recovery/retention policy.
2. Immediate permanent deletion: simplest but removes a valuable user recovery mechanism.
3. **Manual Trash + one current version (selected):** understandable behavior and predictable user control, while preserving basic undelete.

## Consequences and unresolved implementation questions
Trash is not a substitute for crash-consistent writes. A transaction may need temporary or copy-on-write blocks internally to ensure atomicity; those are implementation details, **not user-visible saved versions**. On-disk format, journal/COW choice, root/volume Trash behavior, Trash naming collisions, atomic rename semantics, full-disk handling and recovery tests remain OPEN. Do not silently change previously agreed versioned format compatibility/A-B boot safeguards without a separate review.

## Reconciliation
This ADR supersedes conflicting automatic retention/version-history policy in prior SpiffFS documentation. Preserve historical ADRs for rationale, but mark old behavior superseded in the accepted/open registers and implement this latest approved policy.
