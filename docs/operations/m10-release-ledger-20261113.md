# SpiffOS M10-A–C QEMU release ledger — 2026-11-13

Status at record creation: implementation merged and validated; candidate
staged on Forge, but owner's protected ISO not yet installed. Installation
requires the owner-run exact-SHA script and a subsequent independent check.
M10-D is NOT implemented or authorized.

## Source and review chain
Previous main M9-3D6: `6b4dffbaab20d6717ccb8ea2d85d84e7c0621a62`.
Approved merges in dependency order on 2026-11-13:
- Docs/ADR PR #54: `9f45dba7f0b939f852fbd8be65ff72a73c77a2da`.
- M10-A PR #59: `94441a99b31759d91921bc1eec875a05f888b1f7`.
- M10-B PR #60: `5b1c31b2a194c3de87316526bd134a8a0f8e9f59`.
- M10-C PR #61: `3d5a6d3cedd5eed2a115de726fb0a12b029dc13c`.
GitHub confirmed all four branch heads matched the reviewed SHAs. The
cumulative main tree differs from tested source `fcfda08916459c3395ec05c61215f5ac43dcbe2e`
only in M10 operations documentation.

## Verified backups and package
Forge owner checkout: `/home/geoff/projects/spiffos-qemu-main`.
Owner executed a byte-for-byte `cmp` backup and SHA-256 verification;
remote agent independently verified:
- Current pre-M10 ISO: `build/spiffos.iso`.
- Protected rollback: `build/spiffos.iso.pre-m10-release-20261113-0210`.
- Both SHA-256:
  `758492d3b4aea5587e28514e5882231929e25a90f31c7ce1a38bae8135eb4ba2`.

The owner ISO differs in packaging SHA from the older M9 diagnostic
artifact, but the two M9 kernels have byte-identical content after
stripping debug symbols. Both images booted independently in QEMU and
reported IRQ0 enabled, BOOT_OK and KEYBOARD_READY. Original M9 acceptance
artifact remains `/tmp/spiffos-m9-3d6-tested.iso` with SHA
`06856f9311f7894d5b870add2afd0b457309853433a689e4c74f1f11aa0b5a32`.

Post-merge candidate is staged readable by owner at:
`/tmp/spiffos-m10-abc-merged-3d5a6d3.iso`.
Staged ISO SHA-256:
`ffb0631bbbc0eb4b6885dfe46db158939f75a726c5f3d79fd57ddf9a6c78ab90`.
Embedded kernel SHA-256:
`93a3f08c030fbdfe0dc768646b386c0b34ddcab8fdf202fd2bab1194e6d573ab`.
The embedded kernel matched the separately built ELF byte-for-byte,
and the ELF matched the previously tested M10 source build exactly.
No M10-B diagnostic-only switch symbols were linked into the default
ISO. ISO packaging hashes naturally differ on separate builds.

## Post-merge integration evidence
Work isolated to `/tmp/spiffos-m10c-review-fOoa6o`, exact merged commit
`3d5a6d3cedd5eed2a115de726fb0a12b029dc13c`.
- `make -B HOST_CC=gcc iso`: PASS, private local pinned Limine.
- `make HOST_CC=gcc test-memory test-m10-b-host test-m10-b-guest test-m10-c-guest`:
  PASS task state and scheduler-clock ASan/UBSan tests; GPR sentinel
  host switch test; QEMU 128/256/512 MiB memory and exact framebuffer;
  B 2-task owned-stack switching/failure rollback and post-cleanup
  keyboard; C IRQ reschedule signal consumed only at safe point.
- `make HOST_CC=gcc test-keyboard`: PASS editor/function/special keys.
- `make HOST_CC=gcc test-irq0`: PASS real PIT IRQ0 at 128/256/512 MiB;
  100 ticks in 1.003/1.002/1.004 s; IF=0 disabled-route negative PASS.
- `make test-idt-fault`: PASS real #UD fatal handler.
- `make test-double-fault`: PASS poisoned-stack IST1 #DF.
QEMU uses dedicated OVMF variables and `-net none`; no physical
disks, LAN, OPNsense, host bootloader or system services modified.

## Deployment and rollback
Staged installer: `/tmp/spiffos-m10-abc-merged-install.sh`, for the
`geoff` account. Script checks clean M9 checkout, exact prior source
SHA, matching owner rollback ISO, staged ISO integrity, stopped QEMU,
exact GitHub main, then fast-forwards only and copies/atomically
renames a checksum-verified ISO in the build directory. A failed
preflight does not install anything. On a post-install runtime
regression, stop the SpiffOS QEMU guest and atomically restore the
original `build/spiffos.iso.pre-m10-release-20261113-0210` image,
validating its SHA. Do not reset main or erase records as part of
runtime rollback; revert source separately after investigation.
Perform an independent owner-checkout source SHA, ISO SHA and QEMU
boot/keyboard check after owner installation, then record deployment
success and owner acceptance in a follow-up commit.

## Remaining design boundaries
M10-A policy exists; M10-B execution is a two-task QEMU diagnostic,
not a production general task service. M10-C records timer ticks and
a coalescing safe-point request, not asynchronous preemption.
Guard pages, production task stack manager, priority enforcement and
M10-D are not part of this release.


## Verified owner installation — 2026-11-13
The owner ran `bash /tmp/spiffos-m10-abc-merged-install.sh`; the guarded
installer reported `M10_DEPLOY_OK`, advanced the clean owner worktree to
release ledger commit `8809c15f044f82d8e6c9afc8152980adff37d987`,
and atomically installed the ISO after exact source and hash checks.
Remote independent verification confirmed:
- Owner checkout source exactly `8809c15f044f82d8e6c9afc8152980adff37d987`,
  Git worktree clean.
- Installed `build/spiffos.iso` SHA-256 exactly
  `ffb0631bbbc0eb4b6885dfe46db158939f75a726c5f3d79fd57ddf9a6c78ab90`;
  byte-for-byte matches the staged, tested release candidate.
- Protected `build/spiffos.iso.pre-m10-release-20261113-0210` preserved
  with SHA-256
  `758492d3b4aea5587e28514e5882231929e25a90f31c7ce1a38bae8135eb4ba2`.
- Booted the **installed owner ISO path** in isolated 256 MiB QEMU
  (TCG, fresh temporary OVMF variables, no network, no host disks):
  `SPIFFOS_IRQ0_ENABLED`, `SPIFFOS_BOOT_OK`,
  `SPIFFOS_KEYBOARD_READY` all present; no kernel exception marker.
  Result `INSTALLED_M10_QEMU_BOOT PASS`.
No M10-D scheduler preemption, physical disk/bootloader, network, or
OPNsense changes occurred. Automated acceptance complete. The owner still
needs to manually validate interactive QEMU keyboard, F1–F10 workspace
switching, and the F12 indicator before final human acceptance.
