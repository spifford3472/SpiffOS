# Safe QEMU launcher checkpoint — 2026-10-29

## Scope and recovery
Owner-authorized direct ChatGPT implementation; no delegated agents/models.
Read root AGENTS.md, accepted/open, overview, ADR-011, UEFI/recovery ADR,
keyboard ADR, Developer's Guide and keyboard/workspace-logo operation records.
Verified original editor e3b4f2a, merged-main bbb2d4d, original staged start.S and
four unrelated untracked files. Existing owner QEMU PID 830726 left untouched.
Verified snapshot /home/geoff/projects/spiffos-prescreen-edit-20261029T062442Z:
814 file/link/directory entries, SHA-256 and modes equal before/after/copy.
Manifest is adjacent. Separate feat/safe-qemu-screen-editor worktree based on
bbb2d4d, /home/geoff/projects/spiffos-screen-editor. Original index unchanged.

## Change and rationale
See ADR-safe-qemu-launcher.md. Explicit supported GTK options, absolute-pointer
selection before execution and after boot, visible pointer, window scaling,
window close enabled and recovery instructions. Private QMP, per-run VARS,
read-only firmware CODE, network-disabled guest, existing lock and child cleanup
retained. No change to test/debug commands or guest source in this checkpoint.
No fullscreen or captured desktop session launched by automated validation.

## Actual verification
QEMU 8.2.2 and GTK/SDL installed. Installed manual and QEMU v8.2.2 source inspected.
GTK source verifies Ctrl+Alt+G Grab Input accelerator and relative-click grabbing.
Installed executable parsed the exact GTK options and reached GTK initialization
with an intentionally absent display; this is not a desktop interaction test.
A real QMP probe selected the absolute tablet without guest-driver involvement.

make test-keyboard test-workspace test-workspace-scroll: exit 0, strict kernel
build, sanitizer host tests, real QEMU key injection, exact full-frame initial,
typed, scrolled and forced-scroll screenshots and literal SPIFFOS_BOOT_OK.
Initial isolated build lacked ignored .local-dev/limine. Copied the existing
pinned local dependency without upgrading it; rerun passed.

python3 scripts/test-safe-launcher.py: exit 0. Runs actual launcher with ONLY
GTK_DISPLAY replaced by none in the test harness. Verified guest boot, absolute
pointer after firmware/kernel initialization, 97 injected accepted key bytes,
two full-frame screenshot comparisons, competing runner rejected by lock, QMP
quit exit 0, SIGTERM cleanup, own QEMU child reaped, private VARS/socket directory
removed. Successful second run also demonstrates lock release. QMP quit exercises
the process-exit cleanup path; it does not claim the close button was clicked.
Evidence: build/safe-launcher-validation.log, launcher-close.log,
launcher-signal.log; regression log copied into build/launcher-regression.log.

## Limits and rollback
Actual desktop focus, resizing/scaling, click behavior, shortcut release and
clicking close still require owner observation. The automated launcher validation
does not claim those manual desktop interactions. Roll back by building bbb2d4d
in an isolated worktree; preserve staging, history, drafts and recovery snapshot.
No reset, clean operation or physical-media change is part of this deployment.

## Merge and local QEMU deployment
The owner explicitly authorized merge and deployment on 2026-10-29. PR #14 was
marked ready and merged from exact tested head
9e40e5af09836855508aa39df615ca4c955d719d. GitHub merge commit:
4a06c61f86a5004cb532fdde5096d684e77b265b.

The clean /home/geoff/projects/spiffos-qemu-main checkout fast-forwarded from
bbb2d4d to 4a06c61. Its pinned Limine dependency remained at
7a9013f305de0bea1f6310e76e7baba30499fef0. The merged source rebuilt the local
QEMU ISO and passed make test-keyboard test-workspace test-workspace-scroll,
then python3 scripts/test-safe-launcher.py, all exit 0. This is the deployed
merged QEMU development image; no physical USB, disk or laptop was changed.

Representative typed/scrolled frames had already passed exact pixel checks and
visual inspection. The original editor checkout/index, staged start.S, unrelated
drafts, branding and existing owner QEMU PID 830726 remained untouched.
