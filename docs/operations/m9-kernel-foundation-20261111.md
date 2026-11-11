# M9 kernel foundation — first incremental checkpoint (2026-11-11)

## Baseline
Source: current GitHub main at d1e37dc, isolated checkout /tmp/spiffos-m9-work on Forge.
Owner checkout is geoff-owned and is not writable by the rt-remote development account; neither it nor the existing ISO was changed. The build needed the existing pinned Limine dependency. Read-only symlink plus process-local git safe.directory exception allowed the isolated build without a global trust configuration edit.

The baseline `make test` completed exit 0 on the isolated current source with real QEMU boot and `SPIFFOS_BOOT_OK` marker. The local checkout at /home/geoff/projects/spiffos-qemu-main has older main cbaca54; this did not affect the current source checkout.

## First implementation
The provisional, version-independent internal `kernel_result` enum provides explicit recoverable status values and static names, with no allocation or I/O. This does not modify existing kernel-heap, frame-allocator or memory-service error contracts, and is not yet used by hardware services.

Qwen3-coder:30b generated the initial one-header implementation through local Ollama. Coordinator reviewed it, corrected the full file/function contracts to AGENTS.md conventions, wrote sanitizer unit tests, and integrated a new `test-kernel-result` into `test-memory`.

## Limitations and upcoming work
No interrupt or exception handler, generalized timer, serial driver registry, task engine, new panic path or hardware driver is implemented by this checkpoint. No serial marker change, new kernel objects, configuration change, physical device test or production deployment is claimed. QEMU remains the validation target. Follow ADR-m9-kernel-foundation.md in small tested increments.

## Validation and rollback
The new internal test maps every explicit numeric code and string plus invalid enum fallback. Build with `make test-kernel-result`, run `make test-memory` and `make test` on the verified source before merge. Revert the first M9 PR if necessary; no on-disk format or running kernel state migration is introduced.

## Actual first-checkpoint validation
- `make test` on unmodified current main: PASS, including actual QEMU boot and `SPIFFOS_BOOT_OK`.
- `make test-kernel-result` on M9 branch: PASS under ASan/UBSan.
- `git diff --check`: PASS.
- `make test-memory` on M9 branch: FAIL in 128 MiB QEMU test, no `SPIFFOS_KEYBOARD_READY` serial marker within 30 seconds.
- Direct 128 MiB QEMU retry with `--timeout 60`: FAIL, firmware boot lines printed but no kernel readiness marker. Root cause unresolved. This header-only module is not linked into the kernel, so no causality is attributed without further tests.

**Do not merge or deploy** until the failed QEMU regression is investigated, reproduced against the unchanged baseline at the same 128 MiB configuration, and full acceptance gates pass.

## Unmodified baseline A/B control (same 128 MiB configuration)
A separate detached worktree at d1e37dc, without M9 header, tests or Makefile changes, successfully rebuilt its ISO. Its `python3 scripts/qemu.py test --wait-keyboard-ready --screenshot --memory-mib 128 --timeout 60` also FAILED with no kernel-ready marker after OVMF printed "starting Boot0001". This reproduces the failure on unchanged main and rules out the unlinked M9 header as the immediate cause. The underlying 128 MiB boot failure remains undiagnosed. Keep merge blocked until repaired and full tests pass.
