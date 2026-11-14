# M10-D3.1 / D4 Forge QEMU release — 2026-11-13

Status: **source merged and CI-green; isolated QEMU images built and boot verified; owner installation pending**. The owner approved deployment of the normal D3.1 editor/workspace improvements plus a separate D4 diagnostic environment, with interactive acceptance before M10-D5. Physical boot media, host OS changes, production preemptive editor tasks, OPNsense and network configuration are outside scope.

## Source and governance

The governed project instructions are `AGENTS.md`,
`docs/decisions/accepted.md`, `docs/decisions/open.md`,
`docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-d31-workspaces-debug-hub.md`,
`docs/decisions/ADR-m10-d4-priority-supervisor-proof.md`,
`docs/operations/m10-d4-completion-20261113.md`,
`docs/operations/m10-d31-d4-pr-ci-20261113.md`, and
`docs/DEVELOPERS_GUIDE.md`.

PR #68, PR #69, PR #70 were merged only after each latest-target GitHub CI run passed. The merged implementation source at image build time was
`328a56b13d3b392ea365ee447eb070dbe1faf6f2`.
A later documentation-only commit contains this release ledger; no
C/ASM/Makefile/bootloader source was changed by the documentation.

## Artifacts prepared on Forge

- Normal D3.1/D4 source image (default **nonpreemptive** SpiffOS keyboard/editor):
  `/tmp/spiffos-m10-d4-normal-20261113.iso`;
  SHA-256 `0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`.
- Test-only four-worker M10-D4 preemption diagnostic image:
  `/tmp/spiffos-m10-d4-diagnostic-20261113.iso`;
  SHA-256 `b9c79847fdc63779db65bee8d28ffff2235366311401fadf5d29ea94d92568c6`.
- Default kernel stripped executable SHA-256
  `82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`,
  exactly matching prior accepted D4 normal-kernel binary equivalence
  evidence (the merged source enables D3.1 but not general preemption).
- Owner checkout prior to deployment:
  `/home/geoff/projects/spiffos-qemu-main`,
  source commit `28bbcca650957a0a031611f8e0a16dfe6958d0b3`,
  clean working tree.
- Existing owner ISO SHA-256
  `4fcda82804c72fb4697175b811865755deb3caa4605797bce7a1d7c521eda596`.
- Existing pre-D3 rollback
  `build/spiffos.iso.pre-m10-d3-20261113`, SHA-256
  `ffb0631bbbc0eb4b6885dfe46db158939f75a726c5f3d79fd57ddf9a6c78ab90`.
- Existing M9 backup
  `build/spiffos.iso.pre-m10-release-20261113-0210`,
  SHA-256
  `758492d3b4aea5587e28514e5882231929e25a90f31c7ce1a38bae8135eb4ba2`.
  Both older backups are retained unchanged.

## Validation

The independent postmerge **packaged** ISO tests ran directly
under QEMU TCG with network disabled, private OVMF variables and
memory sizes **128, 256 and 512 MiB**:

- Normal: `SPIFFOS_BOOT_OK`, `SPIFFOS_KEYBOARD_READY`, no
  `SPIFFOS_EXCEPTION` and no D4 diagnostic marker: **PASS 3/3**.
- D4 diagnostic: `SPIFFOS_M10_D4_COMPLETE_OK`,
  `SPIFFOS_KEYBOARD_READY`, `D4_SUPERVISOR_MAX_GAP_TICKS`
  and no kernel exception: **PASS 3/3**.
- Full existing M10-D4 CI job and multi-stage M9/M10 regressions
  passed in GitHub's clean Ubuntu runner before merge.
- The local deployment script
  `/tmp/spiffos-m10-d4-merged-install.sh` passes `bash -n`,
  stages both readable 0644 artifacts and demands exact original
  owner source, checksums and clean checkout.

QWEN was given a very small *read-only* installer safety review.
It highlighted workspace collision, temporary-directory cleanup and
atomic ISO replacement; all are addressed by fail-closed preconditions,
a unique `mktemp` directory with bounded cleanup, an independent
verified backup and an atomic same-filesystem `mv` for the normal ISO.
The coordinator implements/reviews the installer; no Codex fallback
or elevated permission changes were required.

## Exact guarded owner installation

Close any SpiffOS QEMU windows, then as `geoff` on Forge:

```bash
bash /tmp/spiffos-m10-d4-merged-install.sh
```

The script refuses to run as another user or if source/ISO checksums,
rollback images, running QEMU process, clean worktree or untouched D4
destination cannot be established. It preserves the current D3 image in
`build/spiffos.iso.pre-m10-d4-20261113`, fetches exact reviewed
`main`, prepares `/home/geoff/projects/spiffos-qemu-d4-test-20261113`
from a separate exact-source archive and verified test ISO, updates
the owner source via fast-forward only, then atomically replaces the
normal QEMU ISO. Never uses `sudo` or alters group permissions.

Success is reported only by `M10_D4_INSTALL_OK`, including actual
installed source and artifact hashes. Any aborted or nonmatching
installation requires inspection: do not rerun blindly if source or
artifact hashes differ.

## Owner interactive acceptance

Normal D3.1 workspace/F10 build:

```bash
cd /home/geoff/projects/spiffos-qemu-main
python3 scripts/qemu.py run
```

Confirm normal typing/editing on workspaces F1–F9, clean preparation of
all nine identities, F10 read-only real counters (including
`PREPARED WORKSPACES: 9 / 9` and truthful timer data), F12
one-second workspace overlay, returning to previous workspace
with preserved screen content, and normal QEMU window close.
The normal image **does not run D4 preemptive application tasks**.

Separate D4 diagnostic build:

```bash
cd /home/geoff/projects/spiffos-qemu-d4-test-20261113
python3 scripts/qemu.py run
```

The test-only startup runs four priority classes, supervisor
reservation, worker block/wakeup, single-waiter priority inheritance
and validated quarantine/recovery; it must reach the keyboard screen
without a halt. Owner can check the diagnostic console log for
`SPIFFOS_M10_D4_COMPLETE_OK` and per-class slice/latency markers.
Keyboard/editor, F1–F12 and clean close are then tested again.

## Rollback

Stop all QEMU windows. Use the new exact pre-D4 ISO backup
`/home/geoff/projects/spiffos-qemu-main/build/spiffos.iso.pre-m10-d4-20261113`
(hash expected
`4fcda82804c72fb4697175b811865755deb3caa4605797bce7a1d7c521eda596`)
to restore the ordinary ISO via an owner-controlled same-directory
temporary copy and `mv`. Source rollback is separately planned
against exact original commit
`28bbcca650957a0a031611f8e0a16dfe6958d0b3`
without losing the merged history; no automated `git reset --hard`
is authorized. Preserve all backups, release checksums and this ledger.


## Owner deployment and independent verification — 2026-11-13

Owner ran `bash /tmp/spiffos-m10-d4-merged-install.sh` as `geoff`
on Forge and reported successful completion marker `M10_D4_INSTALL_OK`
with source SHA
`896512138eaed698bfb80b6f7f155aa04fca34b7`, normal ISO SHA-256
`0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`,
and separately installed D4 diagnostic ISO SHA-256
`b9c79847fdc63779db65bee8d28ffff2235366311401fadf5d29ea94d92568c6`.
The successful owner-run installer verified both staged and installed
diagnostic hashes and created
`/home/geoff/projects/spiffos-qemu-d4-test-20261113`.

Independent read-only inspection confirmed the owner checkout exactly
at `896512138eaed698bfb80b6f7f155aa04fca34b7`, **clean
working tree**, and installed normal ISO hash matching the release.
All three persistent backups were separately verified unchanged:

- Pre-D4 D3 rollback: `build/spiffos.iso.pre-m10-d4-20261113`,
  SHA-256 `4fcda82804c72fb4697175b811865755deb3caa4605797bce7a1d7c521eda596`.
- Pre-D3 M10 rollback: `build/spiffos.iso.pre-m10-d3-20261113`,
  SHA-256 `ffb0631bbbc0eb4b6885dfe46db158939f75a726c5f3d79fd57ddf9a6c78ab90`.
- Original M9 backup: `build/spiffos.iso.pre-m10-release-20261113-0210`,
  SHA-256 `758492d3b4aea5587e28514e5882231929e25a90f31c7ce1a38bae8135eb4ba2`.

The protected diagnostic workspace is `0700 geoff:geoff`; the
remote read-only account correctly cannot traverse it. We did
**not** weaken its permissions. Its installed contents were
verified by the successful owner installer. The byte-identical
staged D4 diagnostic ISO remains independently readable.

After owner installation, QEMU TCG boot tests (256 MiB) separately
verified **the actually installed normal ISO** and **the verified
staged D4 diagnostic ISO**. Normal guest emitted
`SPIFFOS_BOOT_OK` and `SPIFFOS_KEYBOARD_READY` with no
D4-only marker or exception. D4 guest emitted
`SPIFFOS_M10_D4_COMPLETE_OK`, `D4_SUPERVISOR_MAX_GAP_TICKS`
and `SPIFFOS_KEYBOARD_READY` with no exception or test failure.
Both commands finished successfully with zero exit status. No
production normal ISO or owner workspace modifications were
made during this read-only verification.

Owner interactive GUI acceptance still **PENDING**: F1–F9
workspace isolation and editor, F10 actual prepared/ready/tick counters,
F12 overlay, clean close; repeat keyboard test in D4 diagnostic
workspace. Do not mark D4 owner-accepted or start D5 until
that confirmation. No network configuration, OPNsense, or physical
disk/media change occurred.


## Owner interactive acceptance — 2026-11-13

Following verified owner-run deployment and independent QEMU boot checks,
the owner explicitly reported **"both work"** after launching and testing
both the normal D3.1 SpiffOS image and the separate D4 diagnostic image.
This records acceptance of the requested **two interactive QEMU builds**.
No individual keyboard test transcript was supplied; this acceptance
is the owner's explicit confirmation, not a claim of independently
observed F1–F12 keystrokes.

**M10-D3.1 and M10-D4 QEMU diagnostic milestones are ACCEPTED.**
The D4 four-priority scheduler remains test-only, not a production
preemptive editor or generalized ring-3 task system. The next
separately scoped milestone is **M10-D5**: production scheduling
architecture, process/task isolation, interrupt-state ownership,
recovery, and safe release gates. Do not enable full production
preemption or make physical-hardware changes under this acceptance.
