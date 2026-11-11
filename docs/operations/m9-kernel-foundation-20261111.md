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
